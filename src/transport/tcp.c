#include "transport/tcp.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Phân giải tên theo bài 2-Chap2/domain2ip.c.
static bool make_address(const char *host, uint16_t port, struct sockaddr_in *address) {
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result;
    const int error = getaddrinfo(host, NULL, &hints, &result);
    if (error != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
        return false;
    }

    memcpy(address, result->ai_addr, sizeof(*address));
    address->sin_port = htons(port);
    freeaddrinfo(result);
    return true;
}

// socket, bind, listen và close theo bài 2-Chap2/tcp_server.c.
int open_listener(const char *host, uint16_t port) {
    struct sockaddr_in address;
    if (!make_address(host, port, &address)) {
        return -1;
    }

    const int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) {
        perror("socket");
        return -1;
    }
    if (bind(listener, (const struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind");
        close(listener);
        return -1;
    }
    if (listen(listener, 5) == -1) {
        perror("listen");
        close(listener);
        return -1;
    }
    return listener;
}

// socket, connect và close theo bài 2-Chap2/tcp_client.c.
int connect_to_server(const char *host, uint16_t port) {
    struct sockaddr_in address;
    if (!make_address(host, port, &address)) {
        return -1;
    }

    const int client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == -1) {
        perror("socket");
        return -1;
    }
    if (connect(client, (const struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("connect");
        close(client);
        return -1;
    }
    return client;
}

int local_port(int fd) {
    struct sockaddr_in address = {0};
    socklen_t size = sizeof(address);
    if (getsockname(fd, (struct sockaddr *)&address, &size) == -1) {
        perror("getsockname");
        return -1;
    }
    return ntohs(address.sin_port);
}
