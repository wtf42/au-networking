#pragma once

#include "stream_socket.h"
#include "au_stream_socket_impl.h"


struct au_stream_socket: stream_socket
{
    au_stream_socket(std::shared_ptr<handler_t> handler, sock_t sock);
    ~au_stream_socket();
    void send(const void* buf, size_t size) override;
    void recv(void* buf, size_t size) override;

    std::shared_ptr<handler_t> handler;
    sock_t sock;
};

struct au_stream_server_socket: stream_server_socket
{
    au_stream_server_socket(hostname host, au_stream_port port);
    ~au_stream_server_socket();

    stream_socket* accept_one_client() override;

private:
    std::shared_ptr<handler_t> handler;
    sock_t sock;
};

struct au_stream_client_socket: stream_client_socket
{
    au_stream_client_socket(hostname host, au_stream_port client_port, au_stream_port server_port);
    ~au_stream_client_socket();

    void connect() override;
    void send(const void* buf, size_t size) override;
    void recv(void* buf, size_t size) override;

private:
    std::shared_ptr<handler_t> handler;
    sock_t sock;
};
