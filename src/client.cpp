#include <iostream>
#include <sstream>
#include <queue>

#include <atomic>

#include "common.h"
#include "stream_socket.h"
#include "tcp_socket.h"
#include "au_stream_socket.h"
#include "messages.h"
#include "protocol.h"


enum state_t { WAIT, READY, EXIT };
std::atomic<state_t> state;
pthread_mutex_t state_change_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t state_change_cv = PTHREAD_COND_INITIALIZER;
pthread_scoped_lock_t state_change_lock() { return pthread_scoped_lock_t(&state_change_mutex); }

void set_state(state_t new_state) {
    auto lock = state_change_lock();
    state = new_state;
    pthread_cond_broadcast(&state_change_cv);
}

pthread_mutex_t stdio_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_scoped_lock_t stdio_lock() { return pthread_scoped_lock_t(&stdio_mutex); }

void print(std::string const &msg) {
    auto lock = stdio_lock();
    std::cout << msg << std::endl;
}

std::queue<std::string> events;
pthread_mutex_t events_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t events_cv = PTHREAD_COND_INITIALIZER;
pthread_scoped_lock_t events_lock() { return pthread_scoped_lock_t(&events_mutex); }

void print_safe(std::string const &msg) {
    auto lock = events_lock();
    events.push(msg);
    pthread_cond_signal(&events_cv);
}

std::unique_ptr<stream_client_socket> client_socket;


// Cистема для поиска работы и создания вакансий.
// Клиенты могу добавлять вакансии в систему, а также откликаться с
// произвольным сообщением на уже имеющиеся вакансии.
// Для управления системой клиент может вводить команды:
// search - для поиска работы по подстроке описания вакансии,
//          сервер отвечает пользователю списком найденных вакансий,
//          содержащих номер, имя пользователя и текст описания вакансии
// post - для создания новой вакансии
// reply - для отклика на вакансию с номером, полученным из результатов поиска
// exit - для корректного отключения от сервера и выхода из системы

void help() {
    auto lock = stdio_lock();
    std::cout << "available commands:" << std::endl
              << "  search [text]" << std::endl
              << "  post [text]" << std::endl
              << "  reply [id] [text]" << std::endl
              << "  exit" << std::endl
              << "press enter to write new command" << std::endl
              << std::endl;
}

std::string getName() {
    static std::string result;
    if (!result.empty()) {
        return result;
    }

    srand(time(0));
    for (size_t i = 0 ; i < 8; ++i) {
        result += 'a' + rand() % 26;
    }
    return result;
}

void process_command(std::string const &line) {
    std::stringstream ss(line);
    std::string cmd, arg;
    std::getline(ss, cmd, ' ');
    if (cmd[0] == 's' /*cmd == "search"*/) {
        search_message msg;
        std::getline(ss, arg);
        str_copy(msg.description, arg);
        send(client_socket.get(), msg, search_query_type);
    } else if (cmd[0] == 'p' /*cmd == "post"*/) {
        post_message msg;
        std::getline(ss, arg);
        str_copy(msg.description, arg);
        send(client_socket.get(), msg, post_query_type);
    } else if (cmd[0] == 'r' /*cmd == "reply"*/) {
        reply_message msg;
        std::string id;
        std::getline(ss, id, ' ');
        msg.vacancy_id = (uint16_t)std::stoi(id);
        std::getline(ss, arg);
        str_copy(msg.response, arg);
        send(client_socket.get(), msg, reply_query_type);
    } else if (cmd[0] == 'e' /*cmd == "exit"*/) {
        send(client_socket.get(), exit_query_type);
        set_state(EXIT);
    } else {
        print("invalid command!");
    }
}


void *commands_reader(void*) {
    hello_message msg;
    str_copy(msg.username, getName());
    send(client_socket.get(), msg, hello_query_type);

    if (state == WAIT) {
        print("waiting for hello response...");
        {
            auto lock = state_change_lock();
            while (state == WAIT) {
                pthread_cond_wait(&state_change_cv, lock.mutex);
            }
        }
    }

    while (true) {
        pthread_testcancel();

        std::string line;
        std::getline(std::cin, line);
        {
            auto lock = stdio_lock();
            std::cout << ">";
            std::cout.flush();
            std::getline(std::cin, line);
        }

        process_command(line);
    }
    return NULL;
}


void *events_writer(void*) {
    while (true) {
        pthread_testcancel();

        std::string text;
        {
            auto lock = events_lock();
            while (events.empty()) {
                pthread_cond_wait(&events_cv, lock.mutex);
            }
            text = events.front();
            events.pop();
        }
        print(text);
    }
    return NULL;
}


void process_message(message_types type) {
    std::string text;
    switch (type) {
        case hello_response_type: {
            hello_response_message msg;
            recv(client_socket.get(), msg);
            text = "hello " + getName() + "! we have "
                   + std::to_string(msg.vacancy_count)
                   + " vacancies for you!";
            set_state(READY);
            break;
        }
        case search_response_type: {
            search_response_count_t count;
            recv(client_socket.get(), count);
            text = "found " + std::to_string(count) + " results:";
            for (size_t i = 0; i < count; ++i) {
                search_response_message msg;
                recv(client_socket.get(), msg);
                text += "\nvacancy #"
                        + std::to_string(msg.vacancy_id)
                        + " (from " + msg.username + "): "
                        + msg.description;
            }
            break;
        }
        case post_response_type: {
            post_response_message msg;
            recv(client_socket.get(), msg);
            text = "ok, posted as #" + std::to_string(msg.vacancy_id);
            break;
        }
        case reply_response_type: {
            reply_response_message msg;
            recv(client_socket.get(), msg);
            text = "ok, you also have #"
                   + std::to_string(msg.reply_count)
                   + " competitors!";
            break;
        }
        case reply_notification_type: {
            reply_notification_message msg;
            recv(client_socket.get(), msg);
            text = "you got response for vacancy #"
                   + std::to_string(msg.vacancy_id)
                   + " from " + msg.username
                   + ": " + msg.response;
            break;
        }
        default:
            break;
    }
    if (!text.empty()) {
        print_safe(text);
    }
}


void *socket_reader(void*) {
    try {
        while (true) {
            pthread_testcancel();

            message_type_t type;
            recv(client_socket.get(), type);
            process_message(static_cast<message_types>(type));
        }
    } catch (std::exception const &ex) {
        if (state != EXIT) {
            std::cerr << "failed to read server response: "
                      << ex.what() << std::endl;
            set_state(EXIT);
        }
    }
    return NULL;
}


int main(int argc, char *argv[]) {
    connection_args args = parse_cmdline_args(argc, argv);

    try {
        if (args.au_stream_socket) {
            srand(time(0));
            au_stream_port port = au_stream_port(rand());
            client_socket.reset(new au_stream_client_socket(args.host, port, args.port));
        } else {
            client_socket.reset(new tcp_client_socket(args.host, args.port));
        }
        client_socket->connect();
    } catch (std::exception const &ex) {
        std::cerr << "failed to connect!" << std::endl
                  << ex.what() << std::endl;
        return 1;
    }

    state = WAIT;

    help();

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    pthread_t socket_reader_thread;
    if (pthread_create(&socket_reader_thread, &attr, socket_reader, NULL)) {
        std::cerr << "failed to create socket reader thread" << std::endl;
        return 1;
    }
    pthread_t events_writer_thread;
    if (pthread_create(&events_writer_thread, &attr, events_writer, NULL)) {
        std::cerr << "failed to create events writer thread" << std::endl;
        return 1;
    }
    pthread_t commands_reader_thread;
    if (pthread_create(&commands_reader_thread, &attr, commands_reader, NULL)) {
        std::cerr << "failed to create commands reader thread" << std::endl;
        return 1;
    }

    pthread_attr_destroy(&attr);

    {
        auto lock = state_change_lock();
        while (state != EXIT) {
            pthread_cond_wait(&state_change_cv, lock.mutex);
        }
    }

    pthread_cancel(socket_reader_thread);
    pthread_cancel(events_writer_thread);
    pthread_cancel(commands_reader_thread);
    pthread_join(socket_reader_thread, NULL);
    pthread_join(events_writer_thread, NULL);
    pthread_join(commands_reader_thread, NULL);

    return 0;
}
