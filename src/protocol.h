#pragma once

#include <cstdint>


#pragma pack(1)
struct hello_message {
    char username[16];
};

#pragma pack(1)
struct hello_response_message {
    uint16_t vacancy_count;
};

#pragma pack(1)
struct search_message {
    char description[512];
};

typedef uint8_t search_response_count_t;

#pragma pack(1)
struct search_response_message {
    uint16_t vacancy_id;
    char username[16];
    char description[512];
};

#pragma pack(1)
struct post_message {
    char description[512];
};

#pragma pack(1)
struct post_response_message {
    uint16_t vacancy_id;
};

#pragma pack(1)
struct reply_message {
    uint16_t vacancy_id;
    char response[512];
};

#pragma pack(1)
struct reply_response_message {
    uint16_t reply_count;
};

#pragma pack(1)
struct reply_notification_message {
    uint16_t vacancy_id;
    char username[16];
    char response[512];
};


typedef uint8_t message_type_t;

enum message_types: message_type_t {
    hello_query_type = 0,
    hello_response_type,
    search_query_type,
    search_response_type,
    post_query_type,
    post_response_type,
    reply_query_type,
    reply_response_type,
    reply_notification_type,
    exit_query_type,
};

