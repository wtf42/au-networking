#include "tcp_socket.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <unistd.h>


tcp_server_socket::tcp_server_socket(hostname host, tcp_port port)
        : socket()
{
    addrinfo_provider info(host, port);

    int optval = 1;
    if (setsockopt(socket.fd(), SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval))) {
        throw socket_exception();
    }

    if (::bind(socket.fd(), info.res->ai_addr, info.res->ai_addrlen)) {
        throw socket_exception();
    }

    if (::listen(socket.fd(), 5)) {
        throw socket_exception();
    }
}

stream_socket* tcp_server_socket::accept_one_client() {
    struct sockaddr_storage addr;
    socklen_t addr_size;

    int fd = accept(socket.fd(), (struct sockaddr *)&addr, &addr_size);

    if (fd == -1) {
        throw socket_exception();
    }

    return new tcp_socket(fd);
}



tcp_socket::tcp_socket(int sockfd)
        : sockfd(sockfd)
{}

tcp_socket::tcp_socket(tcp_socket &&rhs)
        : sockfd(rhs.sockfd)
{
    rhs.sockfd = -1;
}

tcp_socket& tcp_socket::operator=(tcp_socket rhs) {
    std::swap(sockfd, rhs.sockfd);
    return *this;
}

tcp_socket::tcp_socket()
        : sockfd(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))
{
    if (sockfd == -1) {
        throw socket_exception();
    }
}

tcp_socket::~tcp_socket() {
    if (sockfd != -1) {
        close(sockfd);
    }
}

void tcp_socket::send(const void* buf, size_t size) {
    ssize_t result = ::send(sockfd, buf, size, 0);
    if (result == -1) {
        throw socket_exception();
    }
}

void tcp_socket::recv(void* buf, size_t size) {
    ssize_t result = ::recv(sockfd, buf, size, 0);
    if (result == -1) {
        throw socket_exception();
    }
    if (result == 0 && size != 0) {
        throw eof_exception();
    }
}



tcp_client_socket::tcp_client_socket(hostname host, tcp_port port)
        : host(host)
        , port(port)
        , socket(-1)
{}

void tcp_client_socket::connect() {
    socket = tcp_socket();
    addrinfo_provider info(host, port);

    if (::connect(socket.fd(), info.res->ai_addr, info.res->ai_addrlen)) {
        throw socket_exception();
    }
}

void tcp_client_socket::send(const void *buf, size_t size)
{
    socket.send(buf, size);
}

void tcp_client_socket::recv(void *buf, size_t size)
{
    socket.recv(buf, size);
}



addrinfo_provider::addrinfo_provider(hostname host, tcp_port port) {
    addrinfo hints;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (int err = getaddrinfo(host, std::to_string(port).c_str(), &hints, &res)) {
        throw socket_exception(gai_strerror(err));
    }
}

addrinfo_provider::~addrinfo_provider() {
    freeaddrinfo(res);
}
