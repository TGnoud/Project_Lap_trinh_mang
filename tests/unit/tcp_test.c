#include "transport/tcp.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
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

static void test_loopback(void) {
    const int listener = open_listener("127.0.0.1", 0);
    assert(listener >= 0);
    const int port = local_port(listener);
    assert(port > 0);

    const int client = connect_to_server("localhost", (uint16_t)port);
    assert(client >= 0);
    const int peer = accept(listener, NULL, NULL);
    assert(peer >= 0);

    const int sockets[] = {client, peer, listener};
    for (size_t i = 0; i < sizeof(sockets) / sizeof(sockets[0]); ++i) {
        assert(close(sockets[i]) == 0);
        assert(fcntl(sockets[i], F_GETFD) == -1 && errno == EBADF);
    }
}

static void test_invalid_endpoints(void) {
    assert(open_listener("::1", 0) == -1);
    assert(connect_to_server("::1", 9000) == -1);
    assert(local_port(-1) == -1);
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

int main(void) {
    test_loopback();
    test_invalid_endpoints();
    test_failed_bind();
    test_refused_connection();
    return 0;
}
