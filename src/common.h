#pragma once

#include <cstdint>
#include <string>

#include "stream_socket.h"

struct connection_args {
    hostname host = "127.0.0.1";
    uint16_t port = 40001;
};


inline connection_args parse_cmdline_args(int argc, char *argv[])
{
    connection_args result;
    if (argc > 1) {
        result.host = argv[1];
    }
    if (argc > 2) {
        result.port = (uint16_t)std::stoi(argv[2]);
    }
    return result;
}

inline void parse_cmdline_args(int argc, char *argv[], const char **addr, uint16_t *port)
{
    connection_args result;
    if (argc > 1) {
        *addr = argv[1];
    }
    if (argc > 2) {
        *port = (uint16_t)std::stoi(argv[2]);
    }
}

