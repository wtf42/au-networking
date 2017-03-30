#include <iostream>
#include <set>
#include <mutex>

#include <pthread.h>

#include "common.h"
#include "stream_socket.h"
#include "tcp_socket.h"

#include "client_handler.h"


std::mutex stdio_mutex;
void print(std::string const &msg) {
    std::lock_guard<std::mutex> lock(stdio_mutex);
    std::cout << msg << std::endl;
}


std::mutex threads_mutex;
std::set<pthread_t> threads;


void *client_handler_func(void *arg) {
    {
        // waiting to store thread_id to set
        std::lock_guard<std::mutex> lock(threads_mutex);
    }

    struct thread_holder_t
    {
        ~thread_holder_t() {
            std::lock_guard<std::mutex> lock(threads_mutex);
            threads.erase(thread);
        }
        pthread_t thread;
    } holder { pthread_self() };

    client_handler(static_cast<stream_socket*>(arg), holder.thread).process_client();

    return NULL;
}


void *socket_listener_func(void *arg) {
    std::unique_ptr<stream_server_socket> server_socket(
            static_cast<stream_server_socket*>(arg));

    try {
        while (true) {
            pthread_testcancel();

            stream_socket *client_socket = server_socket->accept_one_client();
            print("new client connected!");

            {
                std::lock_guard<std::mutex> lock(threads_mutex);
                pthread_t thread;
                pthread_attr_t attr;
                pthread_attr_init(&attr);
                pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
                if (pthread_create(&thread, NULL, client_handler_func, client_socket)) {
                    std::cerr << "failed to create thread!" << std::endl;
                } else {
                    threads.insert(thread);
                }
                pthread_attr_destroy(&attr);
            }
        }
    } catch (std::exception const &ex) {
        std::cerr << "failed to accept new client: " << ex.what() << std::endl;
    }

    return NULL;
}


int main(int argc, char *argv[]) {
    connection_args args = parse_cmdline_args(argc, argv);

    std::unique_ptr<stream_server_socket> socket;

    try {
        socket.reset(new tcp_server_socket(args.host, args.port));
    } catch (std::exception const &ex) {
        std::cerr << "failed to create server socket!" << std::endl
                  << ex.what() << std::endl;
        return 1;
    }

    pthread_t listener_thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    if (pthread_create(&listener_thread, &attr, socket_listener_func, socket.release())) {
        std::cerr << "failed to create socket listener thread" << std::endl;
        return 0;
    }
    pthread_attr_destroy(&attr);

    print("started! press enter to stop server\n");

    std::cin.get();

    pthread_cancel(listener_thread);
    pthread_join(listener_thread, NULL);

    std::set<pthread_t> threads;
    {
        std::lock_guard<std::mutex> lock(threads_mutex);
        threads = ::threads;
    }

    for (auto const &t : threads) {
        pthread_cancel(t);
        pthread_join(t, NULL);
    }

    assert(::threads.size() == 0);

    return 0;
}
