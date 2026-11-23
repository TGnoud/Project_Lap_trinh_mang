#include "transport/frame_io.h"

#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>

static int send_all(int fd, const void *data, size_t size) {
    const unsigned char *bytes = data;
    while (size > 0) {
        const ssize_t sent = send(fd, bytes, size, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent <= 0) {
            return -1;
        }
        bytes += sent;
        size -= (size_t)sent;
    }
    return 0;
}

int send_frame(int fd, const char *body, size_t body_size) {
    unsigned char header[FRAME_HEADER_SIZE];
    if (!frame_encode_header(body_size, header)) {
        errno = EINVAL;
        return -1;
    }
    if (send_all(fd, header, sizeof(header)) < 0) {
        return -1;
    }
    return send_all(fd, body, body_size);
}

int receive_frame(int fd, FrameInput *input) {
    frame_reset(&input->reader);
    for (;;) {
        if (input->offset == input->size) {
            const ssize_t received = recv(fd, input->bytes, sizeof(input->bytes), 0);
            if (received < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("recv");
                return -1;
            }
            if (received == 0) {
                if (frame_at_boundary(&input->reader)) {
                    return 0;
                }
                fputs("Incomplete frame at EOF\n", stderr);
                return -1;
            }
            input->size = (size_t)received;
            input->offset = 0;
        }

        size_t used;
        const FrameStatus status = frame_feed(&input->reader, input->bytes + input->offset,
                                              input->size - input->offset, &used);
        input->offset += used;
        if (status == FRAME_READY) {
            return 1;
        }
        if (status == FRAME_ERROR) {
            fputs("Invalid frame\n", stderr);
            return -1;
        }
    }
}
