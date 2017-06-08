#include <cstring>
#include <netdb.h>
#include <iostream>
#include "au_stream_socket_utils.h"
#include "au_stream_socket.h"

uint16_t calc_inet_checksum(const void* packet, size_t len) {
    size_t bytes_left = len;
    auto half_words = static_cast<const uint16_t *>(packet);
    uint32_t sum = 0;
    /*
     * Our algorithm is simple, using a 32 bit accumulator (sum), we add
     * sequential 16 bit words to it, and at the end, fold back all the
     * carry bits from the top 16 bits into the lower 16 bits.
     */
    while (bytes_left > 1)  {
        sum += *half_words++;
        bytes_left -= 2;
    }
    /* mop up an odd byte, if necessary */
    if (bytes_left == 1) {
        auto last_byte = *reinterpret_cast<const uint8_t*>(half_words);
        sum += last_byte;
    }
    /* add back carry outs from top 16 bits to low 16 bits */
    sum = (sum >> 16) + (sum & 0xffff);	/* add hi 16 to low 16 */
    sum += (sum >> 16);                 /* add carry */
    return (uint16_t) ~sum;             /* truncate to 16 bits */;
}

addrinfo_t get_addrinfo(hostname host, au_stream_port port) {
    addrinfo hints;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_RAW;
    hints.ai_protocol = AU_PROTO;

    addrinfo *info;
    if (int err = getaddrinfo(host, "", &hints, &info)) {
        throw std::runtime_error(gai_strerror(err));
    }

    addrinfo_t result {*info->ai_addr, info->ai_addrlen, port};

    freeaddrinfo(info);

    return result;
}
