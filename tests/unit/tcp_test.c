#include "transport/frame_io.h"
#include "transport/poll_loop.h"
#include "transport/tcp.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

// Keep assertions active in both Debug and Release builds.
#undef NDEBUG
#include <assert.h>

static int open_fd_count(void) {
    // Đọc thư mục bằng opendir/readdir như bài 3-Chap3/udp_server.c.
    DIR *directory = opendir("/proc/self/fd");
    assert(directory != NULL);
    int count = 0;
    while (readdir(directory) != NULL) {
        ++count;
    }
    assert(closedir(directory) == 0);
    return count;
}

static void noop_signal(int signal_number) {
    (void)signal_number;
}

static void test_loopback(void) {
    const int listener = open_listener("127.0.0.1", 0);
    assert(listener >= 0);
    const int port = local_port(listener);
    assert(port > 0);

    const int client = connect_to_server("localhost", (uint16_t)port);
    assert(client >= 0);
    const int peer = accept(listener, NULL, NULL);
    assert(peer >= 0 && set_nonblocking(peer) == 0);

    const int sockets[] = {client, peer, listener};
    for (size_t i = 0; i < sizeof(sockets) / sizeof(sockets[0]); ++i) {
        assert(close(sockets[i]) == 0);
        assert(fcntl(sockets[i], F_GETFD) == -1 && errno == EBADF);
    }
}

static void test_invalid_endpoints(void) {
    static PollServer server;
    assert(open_listener("::1", 0) == -1);
    assert(connect_to_server("::1", 9000) == -1);
    assert(local_port(-1) == -1);
    assert(set_nonblocking(-1) == -1);
    assert(poll_server_init(&server, -1, poll_default_timeouts()) == -1);
}

static void test_failed_bind(void) {
    const int listener = open_listener("127.0.0.1", 0);
    assert(listener >= 0);
    const int port = local_port(listener);
    assert(port > 0);
    const int before = open_fd_count();

    assert(open_listener("127.0.0.1", (uint16_t)port) == -1);
    assert(open_fd_count() == before);
    assert(close(listener) == 0);
}

static void test_refused_connection(void) {
    // Giữ cổng bằng bind, chưa gọi listen để connect nhận lỗi từ chối.
    const int bound_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(bound_socket >= 0);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(bound_socket, (const struct sockaddr *)&address, sizeof(address)) == 0);
    const int port = local_port(bound_socket);
    assert(port > 0);
    const int before = open_fd_count();

    assert(connect_to_server("127.0.0.1", (uint16_t)port) == -1);
    assert(open_fd_count() == before);
    assert(close(bound_socket) == 0);
}

static void test_frame_io_errors(void) {
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    assert(send_frame(pair[0], "", 0) == -1 && errno == EINVAL);
    assert(send_frame(pair[0], "{}", 2) == 0);
    assert(send(pair[0], "\0\0\0\0", 4, 0) == 4);
    assert(close(pair[0]) == 0);

    FrameInput input = {0};
    assert(receive_frame(pair[1], &input) == 1 && strcmp(input.reader.body, "{}") == 0);
    assert(receive_frame(pair[1], &input) == -1);
    assert(send_frame(pair[1], "{}", 2) == -1 && errno == EPIPE);
    frame_reset(&input.reader);
    assert(close(pair[1]) == 0);

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    assert(close(pair[0]) == 0);
    input = (FrameInput){0};
    assert(receive_frame(pair[1], &input) == 0);
    assert(close(pair[1]) == 0);

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    assert(send(pair[0], "\0\0", 2, 0) == 2 && close(pair[0]) == 0);
    assert(receive_frame(pair[1], &input) == -1);
    frame_reset(&input.reader);
    assert(close(pair[1]) == 0);
    assert(receive_frame(-1, &input) == -1);
}

static void test_poll_io_and_backpressure(void) {
    const int before = open_fd_count();
    const int listener = open_listener("127.0.0.1", 0);
    const uint16_t port = (uint16_t)local_port(listener);
    static PollServer server;
    static char large[FRAME_MAX_SIZE];
    memset(large, ' ', sizeof(large));
    memcpy(large, "{}", 2);
    assert(listener >= 0 && port > 0 && poll_server_init(&server, listener, poll_default_timeouts()) == 0);

    // Giữ frame kế tiếp trong RX khi đang có request chạy, rồi xử lý tiếp khi hoàn tất.
    server.defer_requests = true;
    const int client = connect_to_server("127.0.0.1", port);
    assert(client >= 0 && poll_server_step(&server, 1000, 0) == 0);
    assert(send(client, "\0\0\0\7{\"x\":1}\0\0\0\7{\"x\":2}", 22, 0) == 22);
    assert(poll_server_step(&server, 1000, 0) == 0 && server.connections[0].request_active);
    const uint64_t cid = server.connections[0].connection_id;
    assert(!poll_server_complete_request(&server, 999999, "{}", 2, 1000));
    assert(poll_server_complete_request(&server, cid, "{\"ok\":1}", 8, 1000));
    assert(server.connections[0].request_active && strcmp(server.connections[0].reader.body, "{\"x\":2}") == 0);
    assert(poll_server_complete_request(&server, cid, "{\"ok\":2}", 8, 1000));
    assert(poll_server_step(&server, 1000, 0) == 0);

    FrameInput input = {0};
    assert(receive_frame(client, &input) == 1 && strcmp(input.reader.body, "{\"ok\":1}") == 0);
    assert(receive_frame(client, &input) == 1 && strcmp(input.reader.body, "{\"ok\":2}") == 0);
    frame_reset(&input.reader);

    // Offset TX khi gặp EAGAIN, backpressure ở 128 KiB và vượt trần TX 256 KiB.
    server.defer_requests = false;
    const int small = 4096;
    assert(setsockopt(server.connections[0].fd, SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)) == 0);
    assert(setsockopt(client, SOL_SOCKET, SO_RCVBUF, &small, sizeof(small)) == 0);
    assert(!poll_connection_queue_frame(&server.connections[0], "", 0, 1000, 15000));
    assert(poll_connection_queue_frame(&server.connections[0], large, sizeof(large), 1000, 15000));
    assert(poll_connection_queue_frame(&server.connections[0], large, sizeof(large), 1000, 15000));
    assert(poll_connection_backpressured(&server.connections[0]));
    assert(send_frame(client, "{\"after\":1}", 11) == 0 && poll_server_step(&server, 1000, 0) == 0);
    assert(server.connections[0].tx_offset > 0 && server.connections[0].tx_offset < server.connections[0].tx_size);

    size_t remaining = 2 * (FRAME_HEADER_SIZE + sizeof(large));
    while (remaining > 0) {
        char chunk[16384];
        assert(poll_server_step(&server, 1000, 0) == 0);
        const ssize_t got = recv(client, chunk, remaining < sizeof(chunk) ? remaining : sizeof(chunk), MSG_DONTWAIT);
        if (got > 0) remaining -= (size_t)got;
    }
    assert(poll_server_step(&server, 1000, 0) == 0 && poll_server_step(&server, 1000, 0) == 0);
    assert(receive_frame(client, &input) == 1 && strcmp(input.reader.body, "{\"after\":1}") == 0);
    frame_reset(&input.reader);

    for (int i = 0; i < 3; ++i) {
        assert(poll_connection_queue_frame(&server.connections[0], large, sizeof(large), 1000, 15000));
    }
    assert(!poll_connection_queue_frame(&server.connections[0], large, sizeof(large), 1000, 15000));
    assert(!poll_server_complete_request(&server, cid, large, sizeof(large), 1000));
    assert(server.num_connections == 0 && close(client) == 0);

    // HUP/SHUT_WR kèm dữ liệu vẫn nhận đủ phản hồi; vượt trần RX 128 KiB thì đóng.
    const int hup_client = connect_to_server("127.0.0.1", port);
    assert(hup_client >= 0 && poll_server_step(&server, 1000, 0) == 0);
    assert(send_frame(hup_client, "{\"hup\":1}", 9) == 0 && shutdown(hup_client, SHUT_WR) == 0);
    assert(poll_server_step(&server, 1000, 0) == 0 && poll_server_step(&server, 1000, 0) == 0);
    assert(receive_frame(hup_client, &input) == 1 && receive_frame(hup_client, &input) == 0);
    frame_reset(&input.reader);
    assert(close(hup_client) == 0);

    server.defer_requests = true;
    const int rx_client = connect_to_server("127.0.0.1", port);
    assert(rx_client >= 0 && poll_server_step(&server, 1000, 0) == 0);
    for (int i = 0; i < 3; ++i) {
        assert(send_frame(rx_client, large, sizeof(large)) == 0 && poll_server_step(&server, 1000, 0) == 0);
    }
    assert(server.num_connections == 0 && close(rx_client) == 0);

    poll_server_destroy(&server);
    assert(close(listener) == 0 && open_fd_count() == before);
}

static void test_poll_limits_deadlines_and_eintr(void) {
    const int before = open_fd_count();
    const int listener = open_listener("127.0.0.1", 0);
    const uint16_t port = (uint16_t)local_port(listener);
    static PollServer server;
    assert(listener >= 0 && port > 0 && poll_server_init(&server, listener, poll_default_timeouts()) == 0);

    // Lô accept 16 kết nối/lượt, chặn vượt max_connections và kiểm 3 deadline + EINTR.
    int clients[18];
    for (int i = 0; i < 18; ++i) {
        clients[i] = connect_to_server("127.0.0.1", port);
        assert(clients[i] >= 0);
    }
    assert(poll_server_step(&server, 1000, 0) == 0 && server.num_connections == POLL_ACCEPT_BATCH);
    server.max_connections = 17;
    assert(poll_server_step(&server, 1000, 0) == 0 && server.num_connections == 17);

    assert(send(clients[0], "\0\0", 2, 0) == 2 && poll_server_step(&server, 1000, 0) == 0);
    assert(poll_server_step(&server, 11000, 0) == 0 && server.num_connections == 16);

    const int small = 4096;
    static char large[FRAME_MAX_SIZE];
    memset(large, ' ', sizeof(large));
    memcpy(large, "{}", 2);
    assert(setsockopt(server.connections[0].fd, SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)) == 0);
    assert(setsockopt(clients[1], SOL_SOCKET, SO_RCVBUF, &small, sizeof(small)) == 0);
    server.connections[0].authenticated = true;
    assert(poll_connection_queue_frame(&server.connections[0], large, sizeof(large), 1000, 15000));
    assert(poll_connection_queue_frame(&server.connections[0], large, sizeof(large), 1000, 15000));
    assert(poll_server_step(&server, 1000, 0) == 0);
    assert(poll_server_step(&server, 16000, 0) == 0 && server.num_connections == 0);

    for (int i = 0; i < 18; ++i) {
        assert(close(clients[i]) == 0);
    }

    const int leftover = connect_to_server("127.0.0.1", port);
    assert(leftover >= 0 && poll_server_step(&server, monotonic_ms(), 0) == 0 && server.num_connections == 1);

    struct sigaction action = {.sa_handler = noop_signal}, previous = {0};
    const struct itimerval timer = {.it_value = {.tv_usec = 5000}};
    const struct itimerval disarm = {0};
    assert(sigaction(SIGALRM, &action, &previous) == 0);
    assert(setitimer(ITIMER_REAL, &timer, NULL) == 0);
    assert(poll_server_step(&server, monotonic_ms(), 500) == 0);
    assert(setitimer(ITIMER_REAL, &disarm, NULL) == 0);
    assert(sigaction(SIGALRM, &previous, NULL) == 0);

    poll_server_stop(&server);
    poll_server_destroy(&server);
    assert(close(leftover) == 0);
    assert(close(listener) == 0 && open_fd_count() == before);
}

int main(void) {
    test_loopback();
    test_invalid_endpoints();
    test_failed_bind();
    test_refused_connection();
    test_frame_io_errors();
    test_poll_io_and_backpressure();
    test_poll_limits_deadlines_and_eintr();
    return 0;
}
