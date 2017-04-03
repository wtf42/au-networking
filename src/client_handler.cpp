#include <cstdint>
#include <iostream>
#include <vector>
#include <queue>
#include <map>

#include <mutex>

#include "client_handler.h"
#include "messages.h"


struct vacancy {
    struct reply {
        uint16_t vacancy_idx;
        std::string owner_name;
        std::string response;
    };

    uint16_t idx;
    client_id_t owner_id;
    std::string owner_name;
    std::string description;

    std::vector<reply> replies;
};

std::mutex clientsdata_mutex;
std::vector<vacancy> vacancies;
std::map<client_id_t, std::queue<vacancy::reply>> replies;


client_handler::client_handler(stream_socket *socket, client_id_t client_id)
        : socket(socket)
        , client_id(client_id)
{}

void client_handler::process_client() {
    try {
        while (true) {
            pthread_testcancel();

            // После каждого обращения клиента сервер кроме ответа
            // посылает нотификации об откликах на их вакансии.
            if (process_one_reply()) {
                continue;
            }

            // Сервер не предоставляет гарантии доставки сообщения всем клиентам.
            // Поэтому нам не требуется дожидаться ответа об успешном получении
            // сообщения каждым клиентом
            message_type_t type;
            recv(socket.get(), type);
            process_message(static_cast<message_types>(type));

            if (type == exit_query_type) {
                return;
            }
        }
    } catch (std::exception const &ex) {
        // Сервер не пытается восстанавливать соединение с
        // аварийно завершившимся клиентом
        std::cerr << "failed to process client "
                  << name << ": "
                  << ex.what() << std::endl;
    }
}

void client_handler::process_message(message_types type) {
    switch (type) {
        case hello_query_type: {
            hello_message qmsg;
            recv(socket.get(), qmsg);
            name = qmsg.username;
            hello_response_message rmsg;
            {
                std::lock_guard<std::mutex> lock(clientsdata_mutex);
                rmsg.vacancy_count = (uint16_t)vacancies.size();
            }
            send(socket.get(), rmsg, hello_response_type);
            break;
        }
        case search_query_type: {
            search_message qmsg;
            recv(socket.get(), qmsg);

            std::vector<vacancy> found;
            {
                std::lock_guard<std::mutex> lock(clientsdata_mutex);
                for (auto const &v : vacancies) {
                    if (v.description.find(qmsg.description) != std::string::npos) {
                        found.push_back(v);
                    }
                    if (found.size() >= 3) {
                        break;
                    }
                }
            }

            search_response_count_t responses =
                    (search_response_count_t)found.size();
            send(socket.get(), responses, search_response_type);

            for (auto const &result : found) {
                search_response_message rmsg;
                rmsg.vacancy_id = result.idx;
                str_copy(rmsg.username, result.owner_name);
                str_copy(rmsg.description, result.description);
                send(socket.get(), rmsg);
            }
            break;
        }
        case post_query_type: {
            post_message qmsg;
            recv(socket.get(), qmsg);
            post_response_message rmsg;
            {
                std::lock_guard<std::mutex> lock(clientsdata_mutex);
                uint16_t idx = (uint16_t)vacancies.size();
                vacancies.push_back(
                        {
                                idx,
                                client_id,
                                name,
                                qmsg.description,
                                {}
                        });
                rmsg.vacancy_id = idx;
            }
            send(socket.get(), rmsg, post_response_type);
            break;
        }
        case reply_query_type: {
            reply_message qmsg;
            recv(socket.get(), qmsg);
            reply_response_message rmsg;

            vacancy::reply reply = {qmsg.vacancy_id, name, qmsg.response};
            {
                std::lock_guard<std::mutex> lock(clientsdata_mutex);
                if (qmsg.vacancy_id > vacancies.size()) {
                    rmsg.reply_count = 0;
                } else {
                    vacancy &v = vacancies[qmsg.vacancy_id];
                    rmsg.reply_count = (uint16_t)v.replies.size();
                    v.replies.push_back(reply);
                    replies[v.owner_id].push(reply);
                }
            }
            send(socket.get(), rmsg, reply_response_type);
            break;
        }
        case exit_query_type: {
            print("client " + name + " disconnected!");
            break;
        }
        default:
            break;
    }
}

bool client_handler::process_one_reply() {
    vacancy::reply reply;
    {
        std::lock_guard<std::mutex> lock(clientsdata_mutex);
        if (!replies.count(client_id)) {
            return false;
        }
        if (replies[client_id].empty()) {
            return false;
        }
        reply = replies[client_id].front();
        replies[client_id].pop();
    }
    reply_notification_message msg;
    msg.vacancy_id = reply.vacancy_idx;
    str_copy(msg.username, reply.owner_name);
    str_copy(msg.response, reply.response);
    send(socket.get(), msg, reply_notification_type);
    return true;
}
