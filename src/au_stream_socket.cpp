#include "au_stream_socket.h"


au_stream_socket::au_stream_socket(std::shared_ptr<handler_t> handler, sock_t sock)
        : handler(handler)
        , sock(sock)
{}

au_stream_socket::~au_stream_socket() {
    handler->close(sock);
    //handler.reset();
    //handler_t::try_stop();
}

void au_stream_socket::send(const void* buf, size_t size) {
    handler->send(sock, buf, size);
}

void au_stream_socket::recv(void* buf, size_t size) {
    handler->recv(sock, buf, size);
}

au_stream_server_socket::au_stream_server_socket(hostname host, au_stream_port port)
        : handler(handler_t::get())
        , sock(handler->server(host, port))
{}

au_stream_server_socket::~au_stream_server_socket() {
    handler->close(sock);
    //handler.reset();
    //handler_t::try_stop();
}

stream_socket* au_stream_server_socket::accept_one_client() {
    return new au_stream_socket(handler, handler->accept(sock));
}

au_stream_client_socket::au_stream_client_socket(hostname host,
                                                 au_stream_port client_port,
                                                 au_stream_port server_port)
        : handler(handler_t::get())
        , sock(handler->client(host, client_port, server_port))
{}

au_stream_client_socket::~au_stream_client_socket() {
    handler->close(sock);
    //handler.reset();
    //handler_t::try_stop();
}

void au_stream_client_socket::connect() {
    handler->connect(sock);
}

void au_stream_client_socket::send(const void* buf, size_t size) {
    handler->send(sock, buf, size);
}

void au_stream_client_socket::recv(void* buf, size_t size) {
    handler->recv(sock, buf, size);
}
