#pragma once

#include "au_stream_socket_utils.h"
#include "common.h"

#include <list>
#include <cstdint>
#include <pthread.h>
#include <sys/select.h>
#include <memory>


typedef uint16_t au_stream_port;

// Use for experimentation and testing, RFC3692
// http://www.iana.org/assignments/protocol-numbers/
constexpr int AU_PROTO = 254;

struct packet_t;
struct timeout_t;
struct socket_state_t;
typedef socket_state_t *sock_t;

struct handler_t {
    pthread_t handler_thread;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t events = PTHREAD_COND_INITIALIZER;

    static std::shared_ptr<handler_t> get();

    handler_t();
    ~handler_t();

    sock_t client(hostname host, au_stream_port client_port, au_stream_port server_port);
    sock_t server(hostname host, au_stream_port port);

    void connect(sock_t sock);
    sock_t accept(sock_t sock);
    void send(sock_t sock, const void* buf, size_t size);
    void recv(sock_t sock, void* buf, size_t size);
    void close(sock_t sock);

    void handle();

private:
    int evfd;
    int sockfd;
    fd_set rset, wset, eset;

    timeval next_timeout = {0, 0};

    std::list<socket_state_t> sockets;

    void handle_read();
    void handle_write();
    void handle_timeout();
    void handle_events();
    void handle_idle();

    void handle_packet(sock_t sock, packet_t const &packet);
    void handle_timeout(sock_t sock, timeout_t const &event);
    void handle_state_change(sock_t sock);

    bool need_write();
    bool need_write(sock_t sock);
    timeval *get_next_timeout();
    packet_t &next_packet(sock_t sock);
    packet_t &ack_packet(sock_t sock);

    pthread_scoped_lock_t slock();
    void wake();
};
