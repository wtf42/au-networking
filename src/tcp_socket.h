#pragma once

#include "stream_socket.h"

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <cstring>
#include <netdb.h>


typedef uint16_t tcp_port;


struct tcp_socket: stream_socket
{
    tcp_socket(int sockfd);
    tcp_socket();
    tcp_socket(tcp_socket const &) = delete;
    tcp_socket(tcp_socket &&);
    tcp_socket& operator=(tcp_socket);
    ~tcp_socket();

    void send(const void *buf, size_t size) override;
    void recv(void *buf, size_t size) override;

    int fd() const { return sockfd; }

private:
    int sockfd;
};


struct tcp_server_socket: stream_server_socket
{
    tcp_server_socket(hostname host, tcp_port port);

    stream_socket* accept_one_client() override;

private:
    tcp_socket socket;
};


struct tcp_client_socket: stream_client_socket
{
    tcp_client_socket(hostname host, tcp_port port);

    void connect() override;

    void send(const void *buf, size_t size) override;
    void recv(void *buf, size_t size) override;

private:
    hostname host;
    tcp_port port;
    tcp_socket socket;
};


struct socket_exception: std::runtime_error {
    socket_exception(std::string const &msg)
            : std::runtime_error(msg)
    {}
    socket_exception(const char *msg)
            : socket_exception(std::string("socket error: ") + msg)
    {}
    socket_exception()
            : socket_exception(strerror(errno))
    {}
};


struct eof_exception: socket_exception {
    eof_exception()
            : socket_exception(std::string("unexpected eof"))
    {}
};


struct addrinfo_provider {
    addrinfo_provider(hostname host, tcp_port port);
    ~addrinfo_provider();

    addrinfo *res;
};
