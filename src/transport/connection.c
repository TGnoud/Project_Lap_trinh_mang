#include "transport/connection.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct EchoFrame {
    EchoFrame *next;
    size_t size;
    unsigned char bytes[];
};

short connection_events(const Connection *client) {
    short events = 0;
    if (!client->read_eof && client->tx_bytes < TX_PAUSE_BYTES) {
        events |= POLLIN;
    }
    if (client->tx_first != NULL) {
        events |= POLLOUT;
    }
    return events;
}

int64_t connection_deadline(const Connection *client) {
    if (client->rx_deadline != 0 &&
        (client->tx_deadline == 0 || client->rx_deadline < client->tx_deadline)) {
        return client->rx_deadline;
    }
    return client->tx_deadline;
}

bool connection_expired(const Connection *client, int64_t now) {
    if (client->rx_deadline != 0 && now >= client->rx_deadline) {
        fprintf(stderr, "RX frame timeout on fd %d\n", client->fd);
        return true;
    }
    if (client->tx_deadline != 0 && now >= client->tx_deadline) {
        fprintf(stderr, "TX stalled on fd %d\n", client->fd);
        return true;
    }
    return false;
}

static void drop_sent_frame(Connection *client) {
    EchoFrame *frame = client->tx_first;
    client->tx_first = frame->next;
    client->tx_bytes -= sizeof(*frame) + frame->size;
    client->tx_offset = 0;
    free(frame);
    if (client->tx_first == NULL) {
        client->tx_last = NULL;
        client->tx_deadline = 0;
    }
}

void connection_close(Connection *client) {
    frame_reset(&client->rx);
    while (client->tx_first != NULL) {
        drop_sent_frame(client);
    }
    close(client->fd);
    *client = (Connection){.fd = -1};
}

static bool queue_echo(Connection *client, int64_t now) {
    const size_t wire_size = FRAME_HEADER_SIZE + client->rx.body_size;
    const size_t allocation = sizeof(EchoFrame) + wire_size;
    if (allocation > TX_MAX_BYTES - client->tx_bytes) {
        fprintf(stderr, "TX limit on fd %d\n", client->fd);
        return false;
    }
    EchoFrame *frame = malloc(allocation);
    if (frame == NULL) {
        perror("malloc");
        return false;
    }
    frame->next = NULL;
    frame->size = wire_size;
    memcpy(frame->bytes, client->rx.header, FRAME_HEADER_SIZE);
    memcpy(frame->bytes + FRAME_HEADER_SIZE, client->rx.body, client->rx.body_size);
    if (client->tx_last == NULL) {
        client->tx_first = frame;
        client->tx_deadline = now + TX_TIMEOUT_MS;
    } else {
        client->tx_last->next = frame;
    }
    client->tx_last = frame;
    client->tx_bytes += allocation;
    frame_reset(&client->rx);
    client->rx_deadline = 0;
    return true;
}

static bool read_frames(Connection *client, int64_t now) {
    unsigned char buffer[4096];
    size_t quota = IO_QUOTA;
    while (quota != 0 && client->tx_bytes < TX_PAUSE_BYTES) {
        // Chỉ lấy phần còn thiếu, giữ frame tiếp theo trong bộ đệm TCP.
        const FrameReader *rx = &client->rx;
        size_t want = rx->header_used < FRAME_HEADER_SIZE
                          ? FRAME_HEADER_SIZE - rx->header_used
                          : rx->body_size - rx->body_used;
        if (want > sizeof(buffer)) {
            want = sizeof(buffer);
        }
        if (want > quota) {
            want = quota;
        }
        const ssize_t received = recv(client->fd, buffer, want, 0);
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                // Quay lại poll để thử tiếp, đồng thời kiểm deadline và các client khác.
                return true;
            }
            perror("recv");
            return false;
        }
        if (received == 0) {
            if (!frame_at_boundary(rx)) {
                fputs("Incomplete frame at EOF\n", stderr);
                return false;
            }
            client->read_eof = true;
            return true;
        }
        if (client->rx_deadline == 0) {
            client->rx_deadline = now + RX_TIMEOUT_MS;
        }
        quota -= (size_t)received;
        size_t used;
        const FrameStatus status = frame_feed(&client->rx, buffer, (size_t)received, &used);
        if (status == FRAME_ERROR) {
            fputs("Invalid frame\n", stderr);
            return false;
        }
        if (status == FRAME_READY && !queue_echo(client, now)) {
            return false;
        }
    }
    return true;
}

static bool write_frames(Connection *client, int64_t now, bool hung_up) {
    size_t quota = IO_QUOTA;
    while (quota != 0 && client->tx_first != NULL) {
        const EchoFrame *frame = client->tx_first;
        const size_t remaining = frame->size - client->tx_offset;
        const size_t size = remaining < quota ? remaining : quota;
        // Giữ offset như bài 3-Chap3/bt2_tuan3.c, kể cả qua nhiều lượt poll.
        const ssize_t sent = send(client->fd, frame->bytes + client->tx_offset,
                                  size, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) {
                return true;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return !hung_up;
            }
            perror("send");
            return false;
        }
        if (sent == 0) {
            return false;
        }
        client->tx_offset += (size_t)sent;
        quota -= (size_t)sent;
        client->tx_deadline = now + TX_TIMEOUT_MS;
        if (client->tx_offset == frame->size) {
            drop_sent_frame(client);
        }
    }
    return true;
}

bool connection_service(Connection *client, short events, int64_t now) {
    if (events & (POLLERR | POLLNVAL)) {
        return false;
    }
    if ((events & (POLLIN | POLLHUP)) && (connection_events(client) & POLLIN) &&
        !read_frames(client, now)) {
        return false;
    }
    if ((events & (POLLOUT | POLLHUP)) && !write_frames(client, now, events & POLLHUP)) {
        return false;
    }
    return !client->read_eof || client->tx_first != NULL;
}
