#pragma once

#include "stream_socket.h"

#include <cstdint>


typedef uint16_t au_stream_port;


struct au_stream_server_socket: stream_server_socket
{
    au_stream_server_socket(hostname host, au_stream_port port);
};


struct au_stream_client_socket: stream_client_socket
{
    au_stream_client_socket(hostname host, au_stream_port client_port, au_stream_port server_port);
};


