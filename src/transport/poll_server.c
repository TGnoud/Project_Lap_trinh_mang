#include "transport/poll_server.h"

#include "transport/connection.h"
#include "transport/tcp.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

enum { MAX_CLIENTS = 256, ACCEPT_QUOTA = 16, STOP_CHECK_MS = 1000 };

static volatile sig_atomic_t stopping;

static void stop_server(int signal_number) {
    (void)signal_number;
    stopping = 1;
}

static void accept_clients(int listener, Connection clients[MAX_CLIENTS]) {
    size_t slot = 0;
    for (int i = 0; i < ACCEPT_QUOTA && !stopping; ++i) {
        const int fd = accept(listener, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("accept");
            }
            return;
        }
        while (slot < MAX_CLIENTS && clients[slot].fd >= 0) {
            ++slot;
        }
        if (slot == MAX_CLIENTS || set_nonblocking(fd, 1) < 0) {
            close(fd);
            continue;
        }
        clients[slot] = (Connection){.fd = fd};
    }
}

static int poll_timeout(const Connection clients[MAX_CLIENTS], int64_t now) {
    // Giới hạn chờ để xử lý cả SIGTERM ngay trước khi vào poll().
    int64_t wait = STOP_CHECK_MS;
    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        const int64_t deadline = connection_deadline(&clients[i]);
        if (deadline != 0 && deadline - now < wait) {
            wait = deadline - now;
        }
    }
    return wait > 0 ? (int)wait : 0;
}

int run_poll_server(int listener, const char *host, int port) {
    if (set_nonblocking(listener, 1) < 0) {
        perror("fcntl");
        return 1;
    }
    stopping = 0;
    struct sigaction action = {.sa_handler = stop_server};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) < 0) {
        perror("sigaction");
        return 1;
    }

    Connection clients[MAX_CLIENTS] = {0};
    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        clients[i].fd = -1;
    }
    printf("Listening on %s:%d\n", host, port);
    fflush(stdout);
    int status = 0;
    // Listener và các socket client cùng nằm trong mảng pollfd như bài chat room.
    while (!stopping) {
        struct pollfd fds[MAX_CLIENTS + 1] = {{.fd = listener, .events = POLLIN}};
        for (size_t i = 0; i < MAX_CLIENTS; ++i) {
            fds[i + 1].fd = clients[i].fd;
            fds[i + 1].events = connection_events(&clients[i]);
        }
        const int64_t before = monotonic_ms();
        if (before < 0) {
            perror("clock_gettime");
            status = 1;
            break;
        }
        if (poll(fds, MAX_CLIENTS + 1, poll_timeout(clients, before)) < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            status = 1;
            break;
        }
        if (stopping) {
            break;
        }
        const int64_t now = monotonic_ms();
        if (now < 0) {
            perror("clock_gettime");
            status = 1;
            break;
        }
        for (size_t i = 0; i < MAX_CLIENTS; ++i) {
            const short events = fds[i + 1].revents;
            if (clients[i].fd >= 0 &&
                (connection_expired(&clients[i], now) ||
                 (events != 0 && !connection_service(&clients[i], events, now)))) {
                connection_close(&clients[i]);
            }
        }
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fputs("Listening socket failed\n", stderr);
            status = 1;
            break;
        }
        if (fds[0].revents & POLLIN) {
            accept_clients(listener, clients);
        }
    }
    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].fd >= 0) {
            connection_close(&clients[i]);
        }
    }
    return status;
}
