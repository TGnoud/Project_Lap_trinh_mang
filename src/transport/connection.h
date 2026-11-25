#ifndef PLACES_CONNECTION_H
#define PLACES_CONNECTION_H

#include "protocol/frame.h"

#include <stdint.h>

enum {
    RX_TIMEOUT_MS = 10000,
    TX_TIMEOUT_MS = 15000,
    TX_PAUSE_BYTES = 128 * 1024,
    TX_MAX_BYTES = 256 * 1024,
    IO_QUOTA = 64 * 1024,
};

typedef struct EchoFrame EchoFrame;

typedef struct {
    int fd;
    FrameReader rx;
    EchoFrame *tx_first;
    EchoFrame *tx_last;
    size_t tx_bytes;
    size_t tx_offset;
    int64_t rx_deadline;
    int64_t tx_deadline;
    bool read_eof;
} Connection;

// Khởi tạo bằng {.fd = fd}, với fd là socket non-blocking.
short connection_events(const Connection *client);
int64_t connection_deadline(const Connection *client);
bool connection_expired(const Connection *client, int64_t now);
bool connection_service(Connection *client, short events, int64_t now);
void connection_close(Connection *client);

#endif
