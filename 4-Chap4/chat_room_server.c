#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_CLIENTS 10
#define MAX_LINE_SIZE 1024
#define SERVER_PORT 8080
#define LISTEN_BACKLOG 5
#define RECEIVE_BUFFER_SIZE 1024

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void broadcast_message(const int client_sockets[], int num_clients,
                              int sender_socket, const char *message,
                              size_t message_size) {
    for (int i = 0; i < num_clients; i++) {
        if (client_sockets[i] == sender_socket) {
            continue;
        }

        /* This server intentionally makes one non-blocking send attempt. */
        ssize_t sent = send(client_sockets[i], message, message_size, 0);
        if (sent == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                perror("send() failed");
            }
        } else if ((size_t)sent < message_size) {
            fprintf(stderr, "Partial send to client %d (%zd/%zu bytes)\n",
                    client_sockets[i], sent, message_size);
        }
    }
}

static void handle_received_bytes(
    int client_index, int sender_socket, const char *data, size_t data_size,
    const int client_sockets[], int num_clients,
    char line_buffers[MAX_CLIENTS][MAX_LINE_SIZE],
    size_t line_lengths[MAX_CLIENTS], int dropping_line[MAX_CLIENTS]) {
    for (size_t i = 0; i < data_size; i++) {
        char byte = data[i];

        if (dropping_line[client_index]) {
            if (byte == '\n') {
                dropping_line[client_index] = 0;
            }
            continue;
        }

        if (line_lengths[client_index] == MAX_LINE_SIZE) {
            fprintf(stderr,
                    "Discarding overlong line from client %d (limit %d bytes)\n",
                    sender_socket, MAX_LINE_SIZE);
            line_lengths[client_index] = 0;
            dropping_line[client_index] = (byte != '\n');
            continue;
        }

        line_buffers[client_index][line_lengths[client_index]++] = byte;
        if (byte == '\n') {
            broadcast_message(client_sockets, num_clients, sender_socket,
                              line_buffers[client_index],
                              line_lengths[client_index]);
            line_lengths[client_index] = 0;
        }
    }
}

static void remove_client(
    int client_sockets[], char line_buffers[MAX_CLIENTS][MAX_LINE_SIZE],
    size_t line_lengths[MAX_CLIENTS], int dropping_line[MAX_CLIENTS],
    int *num_clients, int client_index) {
    int client_socket = client_sockets[client_index];
    close(client_socket);

    for (int i = client_index; i < *num_clients - 1; i++) {
        client_sockets[i] = client_sockets[i + 1];
        memcpy(line_buffers[i], line_buffers[i + 1], MAX_LINE_SIZE);
        line_lengths[i] = line_lengths[i + 1];
        dropping_line[i] = dropping_line[i + 1];
    }

    (*num_clients)--;
    client_sockets[*num_clients] = -1;
    line_lengths[*num_clients] = 0;
    dropping_line[*num_clients] = 0;
}

static void accept_waiting_clients(
    int listener, int client_sockets[], int *num_clients,
    size_t line_lengths[MAX_CLIENTS], int dropping_line[MAX_CLIENTS]) {
    for (;;) {
        int client = accept(listener, NULL, NULL);
        if (client == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("accept() failed");
            }
            return;
        }

        if (*num_clients >= MAX_CLIENTS) {
            fprintf(stderr, "Maximum clients reached; rejecting socket %d\n",
                    client);
            close(client);
            continue;
        }

        if (set_nonblocking(client) == -1) {
            perror("Could not make client socket non-blocking");
            close(client);
            continue;
        }

        client_sockets[*num_clients] = client;
        line_lengths[*num_clients] = 0;
        dropping_line[*num_clients] = 0;
        (*num_clients)++;
        printf("New client connected: %d (%d/%d)\n", client, *num_clients,
               MAX_CLIENTS);
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) {
        perror("socket() failed");
        return EXIT_FAILURE;
    }

    int reuse_address = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                   sizeof(reuse_address)) == -1) {
        perror("setsockopt() failed");
        close(listener);
        return EXIT_FAILURE;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(SERVER_PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind() failed");
        close(listener);
        return EXIT_FAILURE;
    }

    if (listen(listener, LISTEN_BACKLOG) == -1) {
        perror("listen() failed");
        close(listener);
        return EXIT_FAILURE;
    }

    if (set_nonblocking(listener) == -1) {
        perror("Could not make listening socket non-blocking");
        close(listener);
        return EXIT_FAILURE;
    }

    int client_sockets[MAX_CLIENTS];
    char line_buffers[MAX_CLIENTS][MAX_LINE_SIZE] = {{0}};
    size_t line_lengths[MAX_CLIENTS] = {0};
    int dropping_line[MAX_CLIENTS] = {0};
    int num_clients = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_sockets[i] = -1;
    }

    printf("Chat room server listening on port %d (max %d clients)\n",
           SERVER_PORT, MAX_CLIENTS);

    while (1) {
        struct pollfd poll_fds[MAX_CLIENTS + 1];
        int ready_client_count = num_clients;

        poll_fds[0].fd = listener;
        poll_fds[0].events = POLLIN;
        poll_fds[0].revents = 0;

        for (int i = 0; i < ready_client_count; i++) {
            poll_fds[i + 1].fd = client_sockets[i];
            poll_fds[i + 1].events = POLLIN;
            poll_fds[i + 1].revents = 0;
        }

        int poll_result = poll(poll_fds, (nfds_t)(ready_client_count + 1), -1);
        if (poll_result == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll() failed");
            break;
        }

        if (poll_fds[0].revents & POLLIN) {
            accept_waiting_clients(listener, client_sockets, &num_clients,
                                   line_lengths, dropping_line);
        }

        /* Work backwards so removing a client cannot invalidate pending events. */
        for (int i = ready_client_count - 1; i >= 0; i--) {
            short events = poll_fds[i + 1].revents;
            if (events == 0) {
                continue;
            }

            int client_socket = client_sockets[i];
            int disconnected = 0;
            char receive_buffer[RECEIVE_BUFFER_SIZE];

            if (events & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) {
                for (;;) {
                    ssize_t bytes_received = recv(client_socket, receive_buffer,
                                                  sizeof(receive_buffer), 0);
                    if (bytes_received > 0) {
                        handle_received_bytes(
                            i, client_socket, receive_buffer,
                            (size_t)bytes_received, client_sockets, num_clients,
                            line_buffers, line_lengths, dropping_line);
                        continue;
                    }
                    if (bytes_received == 0) {
                        disconnected = 1;
                        break;
                    }
                    if (errno == EINTR) {
                        continue;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }

                    perror("recv() failed");
                    disconnected = 1;
                    break;
                }
            }

            if (events & (POLLHUP | POLLERR | POLLNVAL)) {
                disconnected = 1;
            }

            if (disconnected) {
                printf("Client %d disconnected\n", client_socket);
                remove_client(client_sockets, line_buffers, line_lengths,
                              dropping_line, &num_clients, i);
            }
        }
    }

    for (int i = 0; i < num_clients; i++) {
        close(client_sockets[i]);
    }
    close(listener);
    return EXIT_FAILURE;
}
