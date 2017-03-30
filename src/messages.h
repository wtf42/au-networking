#pragma once

#include "stream_socket.h"
#include "protocol.h"

template<class T>
void send(stream_socket *socket,
          T const &value,
          typename std::enable_if<std::is_pod<T>::value>::type* = nullptr) {
    socket->send(&value, sizeof(value));
}

template<class T>
void recv(stream_socket *socket,
          T &value,
          typename std::enable_if<std::is_pod<T>::value>::type* = nullptr) {
    socket->recv(&value, sizeof(value));
}

template<class T>
void send(stream_socket *socket,
          T const &value,
          message_type_t message_type) {
    socket->send(&message_type, sizeof(message_type));
    socket->send(&value, sizeof(value));
}

template<size_t N>
void str_copy(char (&dest)[N], std::string const &src) {
    dest[src.copy(dest, N - 1)] = '\0';
}
