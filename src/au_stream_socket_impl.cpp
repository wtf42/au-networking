#include "au_stream_socket_impl.h"
#include "au_stream_socket_utils.h"

//#define LOGURU_IMPLEMENTATION 1
//#include "loguru.hpp"
#define LOG_F(t, ...) {}

#include <signal.h>
#include <deque>
#include <mutex>
#include <netinet/ip.h>
#include <cstring>
#include <netdb.h>
#include <unistd.h>
#include <cassert>
#include <vector>
#include <map>
#include <bitset>
#include <set>
#include <list>
#include <sys/select.h>
#include <sys/time.h>
#include <ctime>
#include <chrono>
#include <sys/eventfd.h>
#include <iostream>
#include <algorithm>
#include <fcntl.h>
#include <sys/ioctl.h>

using std::chrono::system_clock;

struct __attribute__ ((packed)) au_hdr_t {
    au_stream_port src_port;
    au_stream_port dst_port;
    uint8_t flags;
    uint32_t seq_number;
    uint32_t ack_number;
    uint16_t window_size;
    uint16_t data_len;
    uint16_t checksum;
    //uint8_t data[];
};

constexpr uint8_t ACK_FLAG = 1 << 0;
constexpr uint8_t SYN_FLAG = 1 << 1;
constexpr uint8_t FIN_FLAG = 1 << 2;

constexpr size_t AU_MAXPACKET = IP_MAXPACKET;
constexpr size_t AU_MAXDATA = IP_MSS - sizeof(au_hdr_t);
constexpr auto ACK_TIMEOUT = std::chrono::milliseconds(200);
constexpr auto SYN_TIMEOUT = std::chrono::milliseconds(5000);
constexpr auto CLOSE_TIMEOUT = std::chrono::milliseconds(5000);
constexpr auto PERSIST_TIMEOUT = std::chrono::milliseconds(10000);

struct packet_t {
    sockaddr addr;
    socklen_t addr_len;
    std::vector<char> packet;
    bool sent = false;
    bool waitack = false;

    packet_t() {}
    packet_t(addrinfo_t const &addrinfo, const char* data, size_t len)
    : addr(addrinfo.addr), addr_len(addrinfo.addr_len), packet(data, data + len)
    {
    }

    const ip *ip_hdr() const {
        return reinterpret_cast<const ip*>(packet.data());
    }
    const au_hdr_t *au_hdr() const {
        return reinterpret_cast<const au_hdr_t*>(ip_data());
    }
    const char *ip_data() const {
        return packet.data() + (ip_hdr()->ip_hl * 4);
    }
    size_t ip_len() const {
        return packet.size() - (ip_hdr()->ip_hl * 4);
    }
    const char *au_data() const {
        return ip_data() + sizeof(au_hdr_t);
    }

    au_hdr_t *write_hdr() {
        return reinterpret_cast<au_hdr_t*>(&packet[0]);
    }

    void recv(int sockfd) {
        addr_len = sizeof(addr);
        packet.resize(AU_MAXPACKET);
        ssize_t p_len = recvfrom(sockfd, &packet[0], packet.size(),
                                 0, //flags
                                 &addr,
                                 &addr_len);
        if (p_len == -1) {
            throw au_socket_exception();
        }
        packet.resize(size_t(p_len));
    }

    void send(int sockfd) {
        calc_checksum();
        ssize_t p_len = sendto(sockfd, &packet[0], packet.size(),
                               0, //flags
                               &addr,
                               addr_len);
        if (p_len == -1) {
            throw au_socket_exception();
        }
        if (size_t(p_len) != packet.size()) {
            throw au_socket_exception("packet length mismatch");
        }
    }

    void ntoh() {
        ip *hdr = reinterpret_cast<ip*>(&packet[0]);
        hdr->ip_len = ntohs(hdr->ip_len);
        hdr->ip_id = ntohs(hdr->ip_id);
        hdr->ip_off = ntohs(hdr->ip_off);
        hdr->ip_sum = ntohs(hdr->ip_sum);
    }

    void calc_checksum() {
        write_hdr()->checksum = 0;
        write_hdr()->checksum = calc_inet_checksum(packet.data(), packet.size());
    }

    bool valid_checksum() {
        uint16_t old = au_hdr()->checksum;
        uint16_t &checksum = (uint16_t &)au_hdr()->checksum;
        checksum = 0;
        checksum = calc_inet_checksum(ip_data(), ip_len());
        return checksum == old;
    }
};

struct eof_exception: au_socket_exception {
    eof_exception(): au_socket_exception("EOF") {}
};


constexpr size_t SEND_RECV_BUFFER_SIZE = (1 << 16) - 1;
struct send_buffer_t {
    std::deque<char> buf;
    // номер которым должен быть следующий пакет
    // т.е. либо этот покет еще не отправили, либо на него еще не пришел ack
    uint32_t seq_num = 0;
    uint16_t window_size;
};
struct recv_buffer_t {
    std::deque<char> buf;
    // номер которым должен быть следующий пакет
    // получение которого ожидаем, но еще не получили
    uint32_t seq_num = 0;
    struct chunk {
        std::vector<char> data;
        uint32_t seq_num;
        bool operator<(chunk const & rhs) { return seq_num < rhs.seq_num; }
    };
    std::deque<chunk> chunks;
    size_t chunks_len() const {
        size_t len = 0;
        for (auto const &c : chunks) {
            len += c.data.size();
        }
        return len;
    }
    size_t window_size() const {
        return SEND_RECV_BUFFER_SIZE - buf.size();
    }
};
enum timeout_event_t {
    SYN_EVENT, ACK_EVENT, PERSIST_EVENT
};
struct timeout_t {
    std::chrono::system_clock::time_point time;
    timeout_event_t type;
    packet_t *packet;

    bool expired() const {
        return time <= system_clock::now();
    }
};

enum state_t {
    WAITING = 0, LISTEN, SYN_SENT, SYN_RECEIVED, ESTABLISHED, CLOSED
};
struct socket_state_t {
    addrinfo_t src;
    addrinfo_t dst;

    state_t state;
    bool state_changed = false;

    send_buffer_t send_buffer;
    recv_buffer_t recv_buffer;
    std::map<timeout_event_t, std::list<timeout_t>> timeouts;

    std::list<socket_state_t*> accept_queue;

    std::list<packet_t> packets;
};
typedef socket_state_t *sock_t;




std::string ssock(sock_t sock) {
    std::stringstream ss;
    ss << sock->src.port << " -> " << sock->dst.port
       << "(state " << sock->state
       << ",packets " << sock->packets.size()
       << ",send seq " << sock->send_buffer.seq_num
       << ",send size " << sock->send_buffer.buf.size()
       << ",recv seq " << sock->recv_buffer.seq_num
       << ",recv size " << sock->recv_buffer.buf.size()
       << ")";
    return ss.str();
}
std::string spacket(au_hdr_t const &hdr) {
    std::stringstream ss;
    ss << "from " << hdr.src_port
       << " to " << hdr.dst_port
       << "(flags " << int(hdr.flags)
       << ",seq " << hdr.seq_number
       << ",ack " << hdr.ack_number
       << ",data " << hdr.data_len << ")";
    return ss.str();
}
std::string stime(std::chrono::system_clock::time_point t) {
    std::stringstream ss;
    auto tmp = std::chrono::system_clock::to_time_t(t);
    ss << std::ctime(&tmp);
    return ss.str();
}




void *au_sockets_handler(void *arg) {
    reinterpret_cast<handler_t*>(arg)->handle();
    return nullptr;
}
void handler_t::handle() {
    int nfds = std::max(evfd, sockfd) + 1;

    try {
        while (true) {
            pthread_testcancel();

            FD_SET(sockfd, &rset);
            if (need_write()) {
                FD_SET(sockfd, &wset);
            } else {
                FD_CLR(sockfd, &wset);
            }
            FD_SET(sockfd, &eset);
            FD_SET(evfd, &rset);

            timeval *timeout = get_next_timeout();

            int cnt = select(nfds, &rset, &wset, &eset, timeout);
            if (cnt == -1) {
                throw au_socket_exception();
            }

            auto lock = slock();

            if (FD_ISSET(sockfd, &rset)) {
                //LOG_F(INFO, "select read");
                handle_read();
            }
            if (FD_ISSET(sockfd, &wset)) {
                //LOG_F(INFO, "select write");
                handle_write();
            }
            if (cnt == 0) {
                //LOG_F(INFO, "select timeout");
                handle_timeout();
            }
            if (FD_ISSET(evfd, &rset)) {
                //LOG_F(INFO, "select eventfd");
                eventfd_t value;
                eventfd_read(evfd, &value);
                handle_events();
            }
            handle_idle();
        }
    } catch (std::exception const &exception) {
        std::cerr << exception.what() << std::endl;
    }
}

void handler_t::handle_read() {
    packet_t packet;
    packet.recv(sockfd);
    packet.ntoh();
    if (!packet.valid_checksum()) {
        return;
    }
    if (packet.packet.size() != packet.ip_hdr()->ip_len) {
        throw au_socket_exception("packet length mismatch");
    }
    if (packet.ip_hdr()->ip_p != AU_PROTO) {
        throw au_socket_exception("invalid protocol");
    }

    LOG_F(INFO, "packet: %s", spacket(*packet.au_hdr()).c_str());

    // client sockets
    for (auto& s : sockets) {
        if (!sockaddr_eq(s.dst.addr, packet.addr)) continue;
        if (s.src.port != packet.au_hdr()->dst_port) continue;
        if (s.dst.port != packet.au_hdr()->src_port) continue;
        handle_packet(&s, packet);
        return;
    }
    // server sockets
    for (auto& s : sockets) {
        if (s.dst.addr_len != 0) continue;
        if (s.src.port != packet.au_hdr()->dst_port) continue;
        handle_packet(&s, packet);
        return;
    }
    // other process packet
}

void handler_t::handle_write() {
    for (auto& s : sockets) {
        if (need_write(&s)) {
            auto it = s.packets.begin();
            while (it != s.packets.end()) {
                if (it->sent) {
                    ++it;
                    continue;
                }

                it->write_hdr()->window_size = uint16_t(s.recv_buffer.window_size());
                it->write_hdr()->ack_number = s.recv_buffer.seq_num;

                LOG_F(INFO, "write (%s): %s",
                      ssock(&s).c_str(),
                      spacket(*it->write_hdr()).c_str());

                it->send(sockfd);
                it->sent = true;
                if (it->write_hdr()->data_len) {
                    s.send_buffer.window_size -= it->write_hdr()->data_len;
                    s.timeouts[ACK_EVENT].push_back(timeout_t {
                            system_clock::now() + ACK_TIMEOUT,
                            ACK_EVENT,
                            &*it
                    });
                }
                if (!it->waitack) {
                    s.packets.erase(it);
                }
                LOG_F(INFO, "write (%s) end", ssock(&s).c_str());
                return;
            }
        }
    }
}

void handler_t::handle_timeout() {}

void handler_t::handle_events() {}

void handler_t::handle_idle() {
    for (auto& s : sockets) {
        for (auto &tt : s.timeouts) {
            if (!tt.second.empty() && tt.second.front().expired()) {
                handle_timeout(&s, tt.second.front());
                auto it = tt.second.begin();
                while (it != tt.second.end() && it->expired()) {
                    it = tt.second.erase(it);
                }
            }
        }
    }
    for (auto& s : sockets) {
        if (s.state_changed) {
            handle_state_change(&s);
        }
    }
    for (auto& s : sockets) {
        if (s.state != ESTABLISHED) continue;
        while (s.send_buffer.buf.size() && s.send_buffer.window_size) {
            size_t size = AU_MAXDATA;
            size = std::min(size, s.send_buffer.buf.size());
            size = std::min(size, size_t(s.send_buffer.window_size));

            size_t packets_size = 0;
            for (auto &p : s.packets) packets_size += p.write_hdr()->data_len;
            if (packets_size + size > SEND_RECV_BUFFER_SIZE) break;

            packet_t &packet = next_packet(&s);
            packet.waitack = true;
            packet.write_hdr()->data_len = uint16_t(size);
            packet.packet.insert(packet.packet.end(),
                                 s.send_buffer.buf.begin(),
                                 s.send_buffer.buf.begin() + size);
            s.send_buffer.buf.erase(s.send_buffer.buf.begin(),
                                    s.send_buffer.buf.begin() + size);
            s.send_buffer.window_size -= size;

            LOG_F(INFO, "idle (%s): %s",
                  ssock(&s).c_str(),
                  spacket(*packet.write_hdr()).c_str());

            pthread_cond_broadcast(&events);
        }
    }
}

bool handler_t::need_write() {
    for (auto& s : sockets) {
        if (need_write(&s)) {
            return true;
        }
    }
    return false;
}

bool handler_t::need_write(sock_t sock) {
    for (auto &p : sock->packets) {
        if (!p.sent) {
            return true;
        }
    }
    return false;
}

timeval *handler_t::get_next_timeout() {
    system_clock::time_point next;
    bool any = false;
    for (auto& s : sockets) {
        for (auto& tt : s.timeouts) {
            if (tt.second.empty()) continue;
            if (!any || tt.second.front().time < next) {
                next = tt.second.front().time;
                any = true;
            }
        }
    }
    if (any) {
        auto now = system_clock::now();
        next_timeout.tv_sec = 0;
        next_timeout.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(next - now).count();
        if (next_timeout.tv_usec < 0) next_timeout.tv_usec = 0;
        LOG_F(INFO, "timeout: %d", next_timeout.tv_usec);
        return &next_timeout;
    }
    return NULL;
}

sock_t handler_t::client(hostname host, au_stream_port client_port, au_stream_port server_port) {
    LOG_F(INFO, "client constructor %d -> %d", client_port, server_port);
    socket_state_t sock;
    sock.dst = get_addrinfo(host, server_port);
    sock.src.addr_len = 0;
    sock.src.port = client_port;
    sock.state = WAITING;
    auto lock = slock();
    return &*sockets.insert(sockets.end(), sock);
}

sock_t handler_t::server(hostname host, au_stream_port port) {
    LOG_F(INFO, "server constructor %d", port);
    socket_state_t sock;
    sock.src = get_addrinfo(host, port);
    sock.dst.addr_len = 0;
    sock.dst.port = 0;
    sock.state = LISTEN;
    auto lock = slock();
    return &*sockets.insert(sockets.end(), sock);
}

void handler_t::connect(sock_t sock) {
    LOG_F(INFO, "connect (%s)", ssock(sock).c_str());
    auto lock = slock();
    if (sock->state != WAITING) {
        return;
    }

    packet_t &packet = next_packet(sock);
    packet.write_hdr()->flags |= SYN_FLAG;
    packet.waitack = true;

    sock->state = SYN_SENT;
    sock->state_changed = true;

    wake();

    while (sock->state != ESTABLISHED) {
        if (sock->state == CLOSED) {
            throw eof_exception();
        }
        pthread_cond_wait(&events, lock.mutex);
    }
    LOG_F(INFO, "connected!");
}

sock_t handler_t::accept(sock_t sock) {
    LOG_F(INFO, "accept");
    auto lock = slock();
    while (true) {
        if (sock->state != LISTEN) {
            throw au_socket_exception();
        }
        if (!sock->accept_queue.empty()) {
            sock_t client = sock->accept_queue.front();
            sock->accept_queue.pop_front();
            LOG_F(INFO, "accepted!");
            return client;
        }
        pthread_cond_wait(&events, lock.mutex);
    }
    return nullptr;
}

void handler_t::send(sock_t sock, const void* buf, size_t size) {
    LOG_F(INFO, "send (%s) %d bytes", ssock(sock).c_str(), int(size));
    auto lock = slock();
    size_t sent = 0;
    while (sent < size) {
        if (sock->state == CLOSED) {
            throw eof_exception();
        }
        if (sock->send_buffer.buf.size() < SEND_RECV_BUFFER_SIZE) {
            const char *cbuf = (const char*)buf;
            size_t available = std::min(SEND_RECV_BUFFER_SIZE - sock->send_buffer.buf.size(),
                                        size - sent);
            sock->send_buffer.buf.insert(sock->send_buffer.buf.end(),
                                         cbuf + sent,
                                         cbuf + sent + available);
            sent += available;
            wake();
            LOG_F(INFO, "send (%s) sent %d bytes", ssock(sock).c_str(), int(available));
            if (sent >= size) {
                LOG_F(INFO, "send (%s) ok", ssock(sock).c_str());
                return;
            }
        }
        pthread_cond_wait(&events, lock.mutex);
    }
    LOG_F(INFO, "send (%s) ok", ssock(sock).c_str());
}

void handler_t::recv(sock_t sock, void* buf, size_t size) {
    LOG_F(INFO, "recv (%s) %d bytes", ssock(sock).c_str(), int(size));
    auto lock = slock();
    size_t received = 0;
    while (received < size) {
        if (sock->state == CLOSED) {
            throw eof_exception();
        }
        if (sock->recv_buffer.buf.size() > 0) {
            char *cbuf = (char*)buf;
            size_t available = std::min(sock->recv_buffer.buf.size(),
                                        size - received);
            std::copy(sock->recv_buffer.buf.begin(),
                      sock->recv_buffer.buf.begin() + available,
                      cbuf + received);
            sock->recv_buffer.buf.erase(sock->recv_buffer.buf.begin(),
                                        sock->recv_buffer.buf.begin() + available);
            received += available;
            wake();
            LOG_F(INFO, "recv (%s) got %d bytes", ssock(sock).c_str(), int(available));
            if (received >= size) {
                LOG_F(INFO, "recv (%s) ok", ssock(sock).c_str());
                return;
            }
        }
        pthread_cond_wait(&events, lock.mutex);
    }
}

void handler_t::close(sock_t sock) {
    LOG_F(INFO, "close %s", ssock(sock).c_str());
    auto lock = slock();

    packet_t &packet = next_packet(sock);
    packet.write_hdr()->flags |= FIN_FLAG;
    packet.waitack = true;

    sock->state = CLOSED;
    sock->state_changed = true;
    pthread_cond_broadcast(&events);

    wake();
}

void remove_timeout(std::list<timeout_t> &lst, packet_t *packet) {
    auto it = lst.begin();
    while (it != lst.end()) {
        if (it->packet == packet) {
            lst.erase(it);
            return;
        }
        ++it;
    }
}

void handler_t::handle_packet(sock_t sock, packet_t const& packet) {
    LOG_F(INFO, "read (%s): %s", ssock(sock).c_str(), spacket(*packet.au_hdr()).c_str());
    auto hdr = packet.au_hdr();

    switch (sock->state) {
        case WAITING: {
            break;
        }
        case LISTEN: {
            if (hdr->flags & SYN_FLAG) {
                socket_state_t sock_data;
                sock_data.src = sock->src;
                sock_data.dst.addr = packet.addr;
                sock_data.dst.addr_len = packet.addr_len;
                sock_data.dst.port = hdr->src_port;
                sock_data.state = SYN_RECEIVED;
                sock_data.recv_buffer.seq_num = 0;
                sock_data.send_buffer.seq_num = 0;
                sock_data.send_buffer.window_size = hdr->window_size;
                sock_t sock_new = &*sockets.insert(sockets.end(), sock_data);
                sock->accept_queue.push_back(sock_new);
                LOG_F(INFO, "new sock: %s", ssock(sock_new).c_str());

                packet_t &ans_packet = next_packet(sock_new);
                ans_packet.write_hdr()->flags |= SYN_FLAG | ACK_FLAG;
                ans_packet.waitack = true;
                pthread_cond_broadcast(&events);
            }
            break;
        }
        case ESTABLISHED: {
            if (hdr->flags & ACK_FLAG) {
                uint32_t actual = hdr->ack_number;
                uint32_t &expected = sock->send_buffer.seq_num;
                if (actual > expected) {
                    size_t removed = 0;
                    auto it = sock->packets.begin();
                    while (it != sock->packets.end()) {
                        if (it->write_hdr()->seq_number >= actual) break;
                        remove_timeout(sock->timeouts[ACK_EVENT], &*it);
                        it = sock->packets.erase(it);
                        ++removed;
                    }
                    LOG_F(INFO, "got ack %d -> %d (-%d)", expected, actual, removed);
                    expected = actual;
                } else {
                    LOG_F(INFO, "old ack seq");
                }
            }
            if (hdr->data_len) {
                if (hdr->seq_number >= sock->recv_buffer.seq_num) {
                    size_t nbuf_size = hdr->data_len
                                       + sock->recv_buffer.chunks_len()
                                       + sock->recv_buffer.buf.size();
                    if (nbuf_size <= SEND_RECV_BUFFER_SIZE) {
                        sock->recv_buffer.chunks.push_back(recv_buffer_t::chunk{
                                std::vector<char>(packet.au_data(),
                                                  packet.au_data() + hdr->data_len),
                                hdr->seq_number
                        });
                        sort(sock->recv_buffer.chunks.begin(), sock->recv_buffer.chunks.end());
                        auto it = sock->recv_buffer.chunks.begin();
                        while (it != sock->recv_buffer.chunks.end()) {
                            if (it->seq_num != sock->recv_buffer.seq_num) {
                                break;
                            }
                            sock->recv_buffer.buf.insert(sock->recv_buffer.buf.end(),
                                                         it->data.begin(),
                                                         it->data.end());
                            sock->recv_buffer.seq_num += it->data.size();
                            it = sock->recv_buffer.chunks.erase(it);
                        }
                    }
                    pthread_cond_broadcast(&events);
                } else {
                    LOG_F(INFO, "old recv seq %d ~ %d",
                          hdr->seq_number,
                          sock->recv_buffer.seq_num);
                }

                packet_t &answer = ack_packet(sock);
                answer.write_hdr()->flags |= ACK_FLAG;
            }
            if (hdr->flags & FIN_FLAG) {
                LOG_F(INFO, "FIN (%s)", ssock(sock).c_str());
                sock->state = CLOSED;
                sock->state_changed = true;
                pthread_cond_broadcast(&events);

                packet_t &answer = next_packet(sock);
                answer.write_hdr()->flags |= ACK_FLAG | FIN_FLAG;
                answer.waitack = true;
            }
            //todo
            sock->send_buffer.window_size = hdr->window_size;
            break;
        }
        case SYN_SENT: {
            LOG_F(INFO, "SYN_SENT %d", hdr->flags);
            // предусловие состояния - мы отправили один syn
            // ждем syn+ack

            // если syn+ack, то переходим в ESTABLISHED
            if (hdr->flags & SYN_FLAG) {
                sock->state = ESTABLISHED;
                sock->state_changed = true;
                pthread_cond_broadcast(&events);

                assert(sock->packets.front().write_hdr()->flags & SYN_FLAG);
                remove_timeout(sock->timeouts[ACK_EVENT], &sock->packets.front());
                sock->packets.pop_front();

                packet_t &answer = next_packet(sock);
                answer.write_hdr()->flags |= ACK_FLAG;
            }
            sock->send_buffer.window_size = hdr->window_size;
            break;
        }
        case SYN_RECEIVED: {
            LOG_F(INFO, "SYN_RECEIVED %d", hdr->flags);
            // предусловие состояния - мы получили один syn,
            // после чего отправили syn+ack, но хз дошел ли он

            // смотрим не ack ли на наш syn+ack
            // если так, то переходим в ESTABLISHED
            if (hdr->flags & ACK_FLAG) {
                sock->state = ESTABLISHED;
                sock->state_changed = true;
                pthread_cond_broadcast(&events);

                assert(sock->packets.front().write_hdr()->flags & SYN_FLAG);
                remove_timeout(sock->timeouts[ACK_EVENT], &sock->packets.front());
                sock->packets.pop_front();
            }
            sock->send_buffer.window_size = hdr->window_size;
            break;
        }
        case CLOSED: {
            // предусловие состояния:
            // - мы отправили fin
            // или
            // - нам отправили fin и мы его получили

            // нам отправили fin+ack на наш fin, отправляем им ack на это
            if ((hdr->flags & FIN_FLAG) && (hdr->flags & ACK_FLAG)) {
                packet_t &answer = next_packet(sock);
                answer.write_hdr()->flags |= ACK_FLAG;
            }
            break;
        }
    }
    LOG_F(INFO, "read (%s) end", ssock(sock).c_str());
}

uint32_t next_send_seq(sock_t sock) {
    uint32_t seq = sock->send_buffer.seq_num;
    for (auto &p : sock->packets) {
        seq += p.write_hdr()->data_len;
    }
    return seq;
}

packet_t& handler_t::next_packet(sock_t sock) {
    au_hdr_t hdr;
    hdr.src_port = sock->src.port;
    hdr.dst_port = sock->dst.port;
    hdr.flags = 0;
    hdr.seq_number = next_send_seq(sock);
    hdr.ack_number = sock->recv_buffer.seq_num;
    hdr.window_size = uint16_t(sock->recv_buffer.window_size());
    hdr.data_len = 0;
    hdr.checksum = 0;

    return *sock->packets.insert(sock->packets.end(),
                                 packet_t(sock->dst, (char*)&hdr, sizeof(hdr)));
}

packet_t& handler_t::ack_packet(sock_t sock) {
    au_hdr_t hdr;
    hdr.src_port = sock->src.port;
    hdr.dst_port = sock->dst.port;
    hdr.flags = 0;
    hdr.seq_number = next_send_seq(sock);
    hdr.ack_number = sock->recv_buffer.seq_num;
    hdr.window_size = uint16_t(sock->recv_buffer.window_size());
    hdr.data_len = 0;
    hdr.checksum = 0;

    return *sock->packets.insert(sock->packets.end(),
                                 packet_t(sock->dst, (char*)&hdr, sizeof(hdr)));
}

void handler_t::handle_timeout(sock_t sock, timeout_t const& event) {
    LOG_F(INFO, "timeout! %s < %s", stime(system_clock::now()).c_str(), stime(event.time).c_str());
    if (sock->state != ESTABLISHED) return;
    if (event.type != ACK_EVENT) return;
    for (auto &p : sock->packets) {
        p.sent = false;
    }
    wake();
}

void handler_t::handle_state_change(sock_t sock) {
    LOG_F(INFO, "state change: %s", ssock(sock).c_str());
    sock->state_changed = false;
    pthread_cond_broadcast(&events);
}

handler_t::handler_t() {
    if ((sockfd = socket(AF_INET, SOCK_RAW, AU_PROTO)) < 0) {
        throw au_socket_exception();
    }
    if ((evfd = eventfd(0, EFD_NONBLOCK)) < 0) {
        throw au_socket_exception();
    }
    FD_ZERO(&rset);
    FD_ZERO(&wset);
    FD_ZERO(&eset);
    if (pthread_create(&handler_thread, NULL, au_sockets_handler, this)) {
        throw au_socket_exception();
    }
}

handler_t::~handler_t() {
    pthread_cancel(handler_thread);
    pthread_join(handler_thread, NULL);
}

std::shared_ptr<handler_t> handler_ptr;
pthread_mutex_t handler_ptr_mutex;
std::shared_ptr<handler_t> handler_t::get() {
    pthread_scoped_lock_t lock(&handler_ptr_mutex);
    if (!handler_ptr) {
        handler_ptr.reset(new handler_t());
    }
    return handler_ptr;
}

pthread_scoped_lock_t handler_t::slock() { return pthread_scoped_lock_t(&mutex); }

void handler_t::wake() { eventfd_write(evfd, 1); }

