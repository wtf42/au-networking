#pragma once

#include <cstdint>
#include <string>
#include <cstring>
#include <iostream>

#include "stream_socket.h"

struct connection_args {
    hostname host = "127.0.0.1";
    uint16_t port = 40001;
    bool au_stream_socket = false;
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
    const char* env = std::getenv("STREAM_SOCK_TYPE");
    result.au_stream_socket = env && !strncmp(env, "au", 2);
    return result;
}


struct pthread_scoped_lock_t {
    pthread_scoped_lock_t(pthread_mutex_t *mutex): mutex(mutex) {
        pthread_mutex_lock(mutex);
    }
    ~pthread_scoped_lock_t() {
        pthread_mutex_unlock(mutex);
    }
    pthread_mutex_t *mutex;
};
