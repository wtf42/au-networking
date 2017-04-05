#pragma once

#include <pthread.h>
#include <memory>
#include "stream_socket.h"
#include "protocol.h"


void print(std::string const& msg);

struct client_handler
{
    client_handler(stream_socket *socket);

    void process_client();

private:
    void process_message(message_types type);
    bool process_one_reply();

private:
    std::unique_ptr<stream_socket> socket;
    std::string name;
};

