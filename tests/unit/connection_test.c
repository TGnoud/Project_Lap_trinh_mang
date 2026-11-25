#include "transport/connection.h"
#include "transport/frame_io.h"
#include "transport/poll_server.h"
#include "transport/tcp.h"

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#undef NDEBUG
#include <assert.h>

static bool interrupt_recv;
static bool interrupt_send;

ssize_t __real_recv(int fd, void *buffer, size_t size, int flags);
ssize_t __real_send(int fd, const void *buffer, size_t size, int flags);

// Chỉ mô phỏng lần syscall bị ngắt. Các lần còn lại chạy trên socket thật.
ssize_t __wrap_recv(int fd, void *buffer, size_t size, int flags) {
    if (interrupt_recv) {
        interrupt_recv = false;
        errno = EINTR;
        return -1;
    }
    return __real_recv(fd, buffer, size, flags);
}

ssize_t __wrap_send(int fd, const void *buffer, size_t size, int flags) {
    if (interrupt_send) {
        interrupt_send = false;
        errno = EINTR;
        return -1;
    }
    return __real_send(fd, buffer, size, flags);
}

static Connection open_pair(int *peer) {
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    assert(set_nonblocking(fds[0], 1) == 0);
    *peer = fds[1];
    return (Connection){.fd = fds[0]};
}

static void test_rx_timeout_and_hangup(void) {
    int peer;
    Connection client = open_pair(&peer);
    assert(connection_events(&client) == POLLIN);
    assert(connection_deadline(&client) == 0);
    assert(!connection_expired(&client, 100));

    const char wire[] = "\0\0\0\2{}";
    assert(write(peer, wire, 1) == 1);
    interrupt_recv = true;
    assert(connection_service(&client, POLLIN, 100));
    assert(!interrupt_recv && connection_deadline(&client) == 0);
    assert(connection_service(&client, POLLIN, 110));
    assert(client.rx.header_used == 1);
    assert(connection_deadline(&client) == 110 + RX_TIMEOUT_MS);
    assert(write(peer, wire + 1, 1) == 1);
    assert(connection_service(&client, POLLIN, 120));
    assert(connection_deadline(&client) == 110 + RX_TIMEOUT_MS);
    assert(!connection_expired(&client, 109 + RX_TIMEOUT_MS));
    assert(connection_expired(&client, 110 + RX_TIMEOUT_MS));
    connection_close(&client);
    assert(client.fd == -1 && client.rx.body == NULL);
    assert(close(peer) == 0);

    client = open_pair(&peer);
    assert(write(peer, wire, sizeof(wire) - 1) == (ssize_t)sizeof(wire) - 1);
    assert(shutdown(peer, SHUT_WR) == 0);
    // POLLHUP kèm dữ liệu vẫn đọc và echo frame trước khi đóng.
    assert(!connection_service(&client, POLLIN | POLLHUP, 200));
    char response[sizeof(wire) - 1];
    assert(read(peer, response, sizeof(response)) == (ssize_t)sizeof(response));
    assert(memcmp(response, wire, sizeof(response)) == 0);
    assert(client.read_eof && connection_events(&client) == 0);
    assert(!connection_service(&client, POLLNVAL, 200));
    connection_close(&client);
    assert(close(peer) == 0);
}

static void test_tx_queue(void) {
    int peer;
    Connection client = open_pair(&peer);
    const int buffer = 4096;
    assert(setsockopt(client.fd, SOL_SOCKET, SO_SNDBUF, &buffer, sizeof(buffer)) == 0);

    // Hai frame lớn làm TX vượt ngưỡng tạm ngừng RX.
    const size_t frame_size = FRAME_HEADER_SIZE + FRAME_MAX_SIZE;
    static unsigned char wire[2 * (FRAME_HEADER_SIZE + FRAME_MAX_SIZE)];
    static unsigned char output[sizeof(wire)];
    for (size_t i = 0; i < sizeof(wire); i += frame_size) {
        assert(frame_encode_header(FRAME_MAX_SIZE, wire + i));
        memset(wire + i + FRAME_HEADER_SIZE, ' ', FRAME_MAX_SIZE);
        memcpy(wire + i + FRAME_HEADER_SIZE, i == 0 ? "{\"n\":1}" : "{\"n\":2}", 7);
    }
    assert(write(peer, wire, sizeof(wire)) == (ssize_t)sizeof(wire));
    for (int i = 0; i < 3; ++i) {
        assert(connection_service(&client, POLLIN, 100 + i));
    }
    assert(connection_events(&client) == POLLOUT);
    assert(client.tx_bytes >= TX_PAUSE_BYTES && client.tx_bytes <= TX_MAX_BYTES);
    assert(connection_deadline(&client) == 101 + TX_TIMEOUT_MS);

    interrupt_send = true;
    assert(connection_service(&client, POLLOUT, 150));
    assert(!interrupt_send && client.tx_offset == 0);
    assert(connection_service(&client, POLLOUT, 200));
    const size_t offset = client.tx_offset;
    assert(offset > 0 && offset < frame_size);
    assert(connection_service(&client, POLLOUT, 210));
    assert(client.tx_offset == offset && connection_deadline(&client) == 200 + TX_TIMEOUT_MS);
    assert(!connection_expired(&client, 199 + TX_TIMEOUT_MS));
    assert(connection_expired(&client, 200 + TX_TIMEOUT_MS));

    size_t received = 0;
    bool resumed_reads = false;
    for (int attempt = 0; attempt < 64 && received < sizeof(output); ++attempt) {
        const ssize_t got = recv(peer, output + received, sizeof(output) - received, MSG_DONTWAIT);
        if (got > 0) {
            received += (size_t)got;
        } else {
            assert(got == -1 && errno == EAGAIN);
        }
        assert(connection_service(&client, POLLOUT, 220 + attempt));
        resumed_reads |= (connection_events(&client) & (POLLIN | POLLOUT)) == (POLLIN | POLLOUT);
    }
    assert(resumed_reads && received == sizeof(wire));
    assert(memcmp(wire, output, sizeof(wire)) == 0);
    assert(connection_deadline(&client) == 0 && client.tx_bytes == 0);
    assert(connection_events(&client) == POLLIN);
    connection_close(&client);
    assert(close(peer) == 0);
}

static void test_blocking_cli_framing(void) {
    int peer;
    Connection client = open_pair(&peer);
    assert(set_nonblocking(client.fd, 0) == 0);
    FrameInput input = {0};
    interrupt_send = true;
    assert(send_frame(peer, "{}", 2) == 0 && !interrupt_send);
    assert(write(peer, "\0\0", 2) == 2);
    assert(shutdown(peer, SHUT_WR) == 0);

    interrupt_recv = true;
    assert(receive_frame(client.fd, &input) == 1 && !interrupt_recv);
    assert(receive_frame(client.fd, &input) == -1);
    frame_reset(&input.reader);
    connection_close(&client);
    assert(close(peer) == 0);

    client = open_pair(&peer);
    assert(shutdown(peer, SHUT_WR) == 0);
    assert(receive_frame(client.fd, &input) == 0);
    connection_close(&client);
    assert(close(peer) == 0);

    client = open_pair(&peer);
    assert(send_frame(peer, "[]", 2) == 0);
    assert(receive_frame(client.fd, &input) == -1);
    frame_reset(&input.reader);
    connection_close(&client);
    assert(close(peer) == 0);
}

static void test_bad_listener(void) {
    assert(run_poll_server(-1, "127.0.0.1", 0) == 1);
    int pipe_fds[2];
    assert(pipe(pipe_fds) == 0);
    assert(close(pipe_fds[1]) == 0);
    // Một descriptor báo HUP tại vị trí listener phải dừng vòng poll.
    assert(run_poll_server(pipe_fds[0], "127.0.0.1", 0) == 1);
    assert(close(pipe_fds[0]) == 0);
}

int main(void) {
    test_rx_timeout_and_hangup();
    test_tx_queue();
    test_blocking_cli_framing();
    test_bad_listener();
    return 0;
}
