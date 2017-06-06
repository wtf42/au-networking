#pragma once

#include "stream_socket.h"

#include <cstddef>
#include <cstdint>
#include <pthread.h>
#include <sstream>
#include <netinet/ip.h>
#include <chrono>
#include <vector>
#include <string.h>


struct au_socket_exception: std::runtime_error {
    au_socket_exception(std::string const &msg)
            : std::runtime_error(msg)
    {}
    au_socket_exception(const char *msg)
            : au_socket_exception(std::string("socket error: ") + msg)
    {}
    au_socket_exception()
            : au_socket_exception(strerror(errno))
    {}
};

uint16_t calc_inet_checksum(const void* packet, size_t len);

typedef uint16_t au_stream_port;
struct addrinfo_t {
    sockaddr addr;
    socklen_t addr_len;
    au_stream_port port;

    const sockaddr_in *addr_in() {
        return reinterpret_cast<const sockaddr_in*>(&addr);
    }
};
addrinfo_t get_addrinfo(hostname host, au_stream_port port);

inline bool sockaddr_eq(sockaddr const &lhs, sockaddr const &rhs) {
    return reinterpret_cast<const sockaddr_in*>(&lhs)->sin_addr.s_addr ==
           reinterpret_cast<const sockaddr_in*>(&rhs)->sin_addr.s_addr;
}
