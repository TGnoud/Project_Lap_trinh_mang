#include "transport/tcp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// fcntl theo bài 4-Chap4/chat_room_server.c.
int set_nonblocking(int fd, int enabled) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, enabled ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
}

int64_t monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        return -1;
    }
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

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

// Chờ có hạn bằng poll như bài 3-Chap3/udp_client.c. Kiểm SO_ERROR sau khi thức.
static int wait_for_connect(int fd) {
    const int64_t start = monotonic_ms();
    if (start < 0) {
        return -1;
    }
    const int64_t deadline = start + 5000;
    struct pollfd event = {.fd = fd, .events = POLLOUT};
    for (;;) {
        const int64_t now = monotonic_ms();
        if (now < 0) {
            return -1;
        }
        if (now >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }
        const int ready = poll(&event, 1, (int)(deadline - now));
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready == 0) {
            errno = ETIMEDOUT;
            return -1;
        }
        if (ready < 0) {
            return -1;
        }

        int error;
        socklen_t size = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0) {
            return -1;
        }
        if (error != 0) {
            errno = error;
            return -1;
        }
        return 0;
    }
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
    if (set_nonblocking(client, 1) == 0) {
        int connected = connect(client, (const struct sockaddr *)&address, sizeof(address));
        if (connected < 0 && (errno == EINPROGRESS || errno == EINTR)) {
            connected = wait_for_connect(client);
        }
        if (connected == 0 && set_nonblocking(client, 0) == 0) {
            return client;
        }
    }
    const int error = errno;
    perror("connect");
    close(client);
    errno = error;
    return -1;
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
