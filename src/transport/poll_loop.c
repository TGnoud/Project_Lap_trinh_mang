#include "transport/poll_loop.h"
#include "transport/tcp.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

PollTimeouts poll_default_timeouts(void) {
    return (PollTimeouts){
        .frame_timeout_ms = POLL_FRAME_TIMEOUT_MS,
        .tx_timeout_ms = POLL_TX_TIMEOUT_MS,
        .auth_timeout_ms = POLL_AUTH_TIMEOUT_MS,
    };
}

int64_t monotonic_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + (int64_t)(now.tv_nsec / 1000000);
}

int poll_server_init(PollServer *server, int listener, PollTimeouts timeouts) {
    if (set_nonblocking(listener) == -1) {
        return -1;
    }
    memset(server, 0, sizeof(*server));
    server->listener = listener;
    server->max_connections = POLL_MAX_CONNECTIONS;
    server->timeouts = timeouts;
    for (int i = 0; i < POLL_MAX_CONNECTIONS; ++i) {
        server->connections[i].fd = -1;
    }
    return 0;
}

void poll_server_stop(PollServer *server) {
    server->stopping = true;
}

// Đóng fd và dồn mảng kết nối như bài 4-Chap4/chat_room_server.c.
static void close_connection(PollServer *server, int index) {
    PollConnection *connection = &server->connections[index];
    close(connection->fd);
    free(connection->rx_data);
    free(connection->tx_data);
    frame_reset(&connection->reader);

    const int remaining = server->num_connections - index - 1;
    if (remaining > 0) {
        memmove(&server->connections[index], &server->connections[index + 1],
                (size_t)remaining * sizeof(server->connections[0]));
    }
    --server->num_connections;
    server->connections[server->num_connections] = (PollConnection){.fd = -1};
}

void poll_server_destroy(PollServer *server) {
    for (int i = server->num_connections - 1; i >= 0; --i) {
        close_connection(server, i);
    }
}

// Cấp phát bộ đệm động theo nhu cầu như bài 1-Chap1/lab03_dynamic_array.c.
static bool append_bytes(unsigned char **buffer, size_t *size, size_t *offset, size_t *capacity,
                         const void *data, size_t data_size, size_t max_size) {
    const size_t pending = *size - *offset;
    if (data_size > max_size || pending > max_size - data_size) {
        return false;
    }
    if (*offset > 0) {
        if (pending > 0) {
            memmove(*buffer, *buffer + *offset, pending);
        }
        *size = pending;
        *offset = 0;
    }
    if (*size + data_size > *capacity) {
        size_t next_capacity = *capacity == 0 ? 1024 : *capacity;
        while (next_capacity < *size + data_size) {
            next_capacity = next_capacity <= max_size / 2 ? next_capacity * 2 : max_size;
        }
        unsigned char *grown = realloc(*buffer, next_capacity);
        if (grown == NULL) {
            return false;
        }
        *buffer = grown;
        *capacity = next_capacity;
    }
    memcpy(*buffer + *size, data, data_size);
    *size += data_size;
    return true;
}

bool poll_connection_backpressured(const PollConnection *connection) {
    return connection->tx_size - connection->tx_offset >= POLL_TX_BACKPRESSURE;
}

bool poll_connection_queue_frame(PollConnection *connection, const char *body, size_t body_size,
                                 int64_t now_ms, int tx_timeout_ms) {
    unsigned char header[FRAME_HEADER_SIZE];
    if (!frame_encode_header(body_size, header)) {
        return false;
    }
    if (!append_bytes(&connection->tx_data, &connection->tx_size, &connection->tx_offset,
                      &connection->tx_capacity, header, sizeof(header),
                      POLL_TX_MAX_SIZE - body_size) ||
        !append_bytes(&connection->tx_data, &connection->tx_size, &connection->tx_offset,
                      &connection->tx_capacity, body, body_size, POLL_TX_MAX_SIZE)) {
        return false;
    }
    if (connection->tx_deadline_ms == 0) {
        connection->tx_deadline_ms = now_ms + tx_timeout_ms;
    }
    return true;
}

static void update_frame_deadline(const PollServer *server, PollConnection *connection,
                                  int64_t now_ms) {
    const bool has_partial = !frame_at_boundary(&connection->reader) ||
                             (!connection->request_active &&
                              connection->rx_offset < connection->rx_size);
    if (!has_partial) {
        connection->frame_deadline_ms = 0;
    } else if (connection->frame_deadline_ms == 0) {
        connection->frame_deadline_ms = now_ms + server->timeouts.frame_timeout_ms;
    }
}

static int process_rx_buffer(PollServer *server, PollConnection *connection, int64_t now_ms) {
    while (!connection->request_active && !poll_connection_backpressured(connection) &&
           connection->rx_offset < connection->rx_size) {
        size_t used = 0;
        const FrameStatus status = frame_feed(
            &connection->reader, connection->rx_data + connection->rx_offset,
            connection->rx_size - connection->rx_offset, &used);
        connection->rx_offset += used;
        if (connection->rx_offset == connection->rx_size) {
            connection->rx_offset = 0;
            connection->rx_size = 0;
        }
        if (status == FRAME_ERROR) {
            fputs("Invalid frame\n", stderr);
            return -1;
        }
        if (status == FRAME_MORE) {
            break;
        }

        connection->frame_deadline_ms = 0;
        if (server->defer_requests) {
            connection->request_active = true;
            break;
        }
        if (!poll_connection_queue_frame(connection, connection->reader.body,
                                         connection->reader.body_size, now_ms,
                                         server->timeouts.tx_timeout_ms)) {
            fputs("TX buffer limit exceeded\n", stderr);
            return -1;
        }
        frame_reset(&connection->reader);
    }

    update_frame_deadline(server, connection, now_ms);
    if (connection->peer_eof && connection->rx_offset == connection->rx_size &&
        !frame_at_boundary(&connection->reader)) {
        fputs("Incomplete frame at EOF\n", stderr);
        return -1;
    }
    return 0;
}

bool poll_server_complete_request(PollServer *server, uint64_t connection_id,
                                  const char *body, size_t body_size, int64_t now_ms) {
    for (int i = 0; i < server->num_connections; ++i) {
        PollConnection *connection = &server->connections[i];
        if (connection->connection_id != connection_id) {
            continue;
        }
        frame_reset(&connection->reader);
        connection->request_active = false;
        if (!poll_connection_queue_frame(connection, body, body_size, now_ms,
                                         server->timeouts.tx_timeout_ms)) {
            fputs("TX buffer limit exceeded\n", stderr);
            close_connection(server, i);
            return false;
        }
        if (process_rx_buffer(server, connection, now_ms) < 0) {
            close_connection(server, i);
            return false;
        }
        return true;
    }
    return false;
}

static int handle_client_read(PollServer *server, PollConnection *connection, int64_t now_ms) {
    size_t budget = POLL_IO_CHUNK_SIZE;
    while (budget > 0 && !poll_connection_backpressured(connection)) {
        const ssize_t received = recv(connection->fd, server->io_buffer, budget, 0);
        if (received > 0) {
            budget -= (size_t)received;
            const size_t in_reader = connection->reader.header_used + connection->reader.body_used;
            if (!append_bytes(&connection->rx_data, &connection->rx_size, &connection->rx_offset,
                              &connection->rx_capacity, server->io_buffer, (size_t)received,
                              POLL_RX_MAX_SIZE - in_reader)) {
                fputs("RX buffer limit exceeded\n", stderr);
                return -1;
            }
            if (process_rx_buffer(server, connection, now_ms) < 0) {
                return -1;
            }
            continue;
        }
        if (received == 0) {
            connection->peer_eof = true;
            return process_rx_buffer(server, connection, now_ms);
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        perror("recv");
        return -1;
    }
    return 0;
}

// Giữ phần gửi dở và offset TX qua POLLOUT, bổ sung cho bài 4-Chap4/chat_room_server.c.
static int handle_client_write(PollServer *server, PollConnection *connection, int64_t now_ms) {
    size_t budget = POLL_IO_CHUNK_SIZE;
    while (budget > 0 && connection->tx_offset < connection->tx_size) {
        const size_t pending = connection->tx_size - connection->tx_offset;
        const size_t chunk = pending < budget ? pending : budget;
        const ssize_t sent = send(connection->fd, connection->tx_data + connection->tx_offset,
                                  chunk, MSG_NOSIGNAL);
        if (sent > 0) {
            connection->tx_offset += (size_t)sent;
            budget -= (size_t)sent;
            if (connection->tx_offset == connection->tx_size) {
                connection->tx_offset = 0;
                connection->tx_size = 0;
                connection->tx_deadline_ms = 0;
            } else {
                connection->tx_deadline_ms = now_ms + server->timeouts.tx_timeout_ms;
            }
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        perror("send");
        return -1;
    }
    if (!poll_connection_backpressured(connection) &&
        connection->rx_offset < connection->rx_size) {
        return process_rx_buffer(server, connection, now_ms);
    }
    return 0;
}

// Chấp nhận tối đa 16 kết nối non-blocking mỗi lượt như bài 4-Chap4/chat_room_server.c.
static void accept_waiting_clients(PollServer *server, int64_t now_ms) {
    for (int accepted = 0; accepted < POLL_ACCEPT_BATCH; ++accepted) {
        const int client = accept(server->listener, NULL, NULL);
        if (client == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("accept");
            }
            return;
        }
        if (server->num_connections >= server->max_connections) {
            fputs("Maximum clients reached\n", stderr);
            close(client);
            continue;
        }
        if (set_nonblocking(client) == -1) {
            close(client);
            continue;
        }
        PollConnection *connection = &server->connections[server->num_connections++];
        *connection = (PollConnection){
            .fd = client,
            .connection_id = ++server->next_connection_id,
            .auth_deadline_ms = now_ms + server->timeouts.auth_timeout_ms,
        };
        puts("Client connected");
        fflush(stdout);
    }
}

static void consider_deadline(int64_t deadline_ms, int64_t *nearest_ms) {
    if (deadline_ms > 0 && (*nearest_ms == 0 || deadline_ms < *nearest_ms)) {
        *nearest_ms = deadline_ms;
    }
}

static int compute_poll_timeout(const PollServer *server, int64_t now_ms, int max_wait_ms) {
    int64_t nearest_ms = 0;
    for (int i = 0; i < server->num_connections; ++i) {
        const PollConnection *connection = &server->connections[i];
        consider_deadline(connection->frame_deadline_ms, &nearest_ms);
        consider_deadline(connection->tx_deadline_ms, &nearest_ms);
        consider_deadline(connection->auth_deadline_ms, &nearest_ms);
    }
    int timeout_ms = -1;
    if (nearest_ms > 0) {
        const int64_t delta_ms = nearest_ms - now_ms;
        timeout_ms = delta_ms <= 0 ? 0 : (int)delta_ms;
    }
    if (max_wait_ms >= 0 && (timeout_ms < 0 || max_wait_ms < timeout_ms)) {
        timeout_ms = max_wait_ms;
    }
    return timeout_ms;
}

static bool connection_expired(const PollConnection *connection, int64_t now_ms) {
    if (connection->frame_deadline_ms > 0 && now_ms >= connection->frame_deadline_ms) {
        fputs("Frame timeout\n", stderr);
        return true;
    }
    if (connection->tx_deadline_ms > 0 && now_ms >= connection->tx_deadline_ms) {
        fputs("TX timeout\n", stderr);
        return true;
    }
    if (!connection->authenticated && !connection->auth_job_active &&
        connection->auth_deadline_ms > 0 && now_ms >= connection->auth_deadline_ms) {
        fputs("Auth timeout\n", stderr);
        return true;
    }
    return false;
}

// Vòng poll() theo dõi POLLIN/POLLOUT như bài 4-Chap4/chat_room_server.c.
int poll_server_step(PollServer *server, int64_t now_ms, int max_wait_ms) {
    struct pollfd poll_fds[POLL_MAX_CONNECTIONS + 1];
    const int ready_count = server->num_connections;

    poll_fds[0].fd = server->stopping ? -1 : server->listener;
    poll_fds[0].events = POLLIN;
    poll_fds[0].revents = 0;

    for (int i = 0; i < ready_count; ++i) {
        const PollConnection *connection = &server->connections[i];
        short events = 0;
        if (!connection->peer_eof && !poll_connection_backpressured(connection)) {
            events |= POLLIN;
        }
        if (connection->tx_offset < connection->tx_size) {
            events |= POLLOUT;
        }
        poll_fds[i + 1].fd = connection->fd;
        poll_fds[i + 1].events = events;
        poll_fds[i + 1].revents = 0;
    }

    const int timeout_ms = compute_poll_timeout(server, now_ms, max_wait_ms);
    if (poll(poll_fds, (nfds_t)(ready_count + 1), timeout_ms) == -1) {
        if (errno == EINTR) {
            return 0;
        }
        perror("poll");
        return -1;
    }
    if (timeout_ms != 0) {
        now_ms = monotonic_ms();
    }

    if (poll_fds[0].revents & POLLIN) {
        accept_waiting_clients(server, now_ms);
    }

    for (int i = ready_count - 1; i >= 0; --i) {
        PollConnection *connection = &server->connections[i];
        const short revents = poll_fds[i + 1].revents;
        bool should_close = (revents & (POLLERR | POLLNVAL)) != 0 ||
                            connection_expired(connection, now_ms);

        if (!should_close && (revents & POLLOUT) &&
            handle_client_write(server, connection, now_ms) < 0) {
            should_close = true;
        }
        if (!should_close && (revents & (POLLIN | POLLHUP)) && !connection->peer_eof &&
            !poll_connection_backpressured(connection) &&
            handle_client_read(server, connection, now_ms) < 0) {
            should_close = true;
        }
        if (!should_close && connection->peer_eof &&
            connection->rx_offset == connection->rx_size &&
            connection->tx_offset == connection->tx_size && !connection->request_active) {
            should_close = true;
        }
        if (should_close) {
            close_connection(server, i);
        }
    }
    return 0;
}
