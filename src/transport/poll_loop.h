#ifndef PLACES_POLL_LOOP_H
#define PLACES_POLL_LOOP_H

#include "protocol/frame.h"
#include "transport/worker.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    POLL_MAX_CONNECTIONS = 256,
    POLL_ACCEPT_BATCH = 16,
    POLL_IO_CHUNK_SIZE = 64 * 1024,
    POLL_RX_MAX_SIZE = 128 * 1024,
    POLL_TX_BACKPRESSURE = 128 * 1024,
    POLL_TX_MAX_SIZE = 256 * 1024,
    POLL_FRAME_TIMEOUT_MS = 10000,
    POLL_TX_TIMEOUT_MS = 15000,
    POLL_AUTH_TIMEOUT_MS = 15000,
};

typedef struct {
    int frame_timeout_ms;
    int tx_timeout_ms;
    int auth_timeout_ms;
} PollTimeouts;

typedef struct {
    int fd;
    uint64_t connection_id;
    bool authenticated;
    bool auth_job_active;
    bool request_active;
    bool peer_eof;
    unsigned char *rx_data;
    size_t rx_size;
    size_t rx_offset;
    size_t rx_capacity;
    FrameReader reader;
    unsigned char *tx_data;
    size_t tx_size;
    size_t tx_offset;
    size_t tx_capacity;
    int64_t frame_deadline_ms;
    int64_t tx_deadline_ms;
    int64_t auth_deadline_ms;
} PollConnection;

typedef enum {
    POLL_ROUTE_BUSINESS,
    POLL_ROUTE_AUTH,
} PollRoute;

typedef PollRoute (*PollRouteFn)(const char *body, size_t body_size);

typedef struct {
    int listener;
    int max_connections;
    int num_connections;
    uint64_t next_connection_id;
    bool stopping;
    bool defer_requests;
    PollTimeouts timeouts;
    Worker *business_worker;
    Worker *auth_worker;
    PollRouteFn route_request;
    PollConnection connections[POLL_MAX_CONNECTIONS];
    unsigned char io_buffer[POLL_IO_CHUNK_SIZE];
} PollServer;

PollTimeouts poll_default_timeouts(void);
int64_t monotonic_ms(void);

int poll_server_init(PollServer *server, int listener, PollTimeouts timeouts);
void poll_server_stop(PollServer *server);
void poll_server_destroy(PollServer *server);

bool poll_connection_backpressured(const PollConnection *connection);
bool poll_connection_queue_frame(PollConnection *connection, const char *body, size_t body_size,
                                 int64_t now_ms, int tx_timeout_ms);
bool poll_server_complete_request(PollServer *server, uint64_t connection_id,
                                  const char *body, size_t body_size, int64_t now_ms);

/* Runs one poll() iteration. Pass max_wait_ms < 0 to wait until the nearest deadline. */
int poll_server_step(PollServer *server, int64_t now_ms, int max_wait_ms);

#endif
