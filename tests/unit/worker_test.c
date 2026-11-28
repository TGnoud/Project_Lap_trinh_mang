#include "transport/frame_io.h"
#include "transport/poll_loop.h"
#include "transport/tcp.h"
#include "transport/worker.h"

#include <dirent.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// Keep assertions active in both Debug and Release builds.
#undef NDEBUG
#include <assert.h>

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool open;
    int started;
    int completed;
} TestGate;

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

static void gate_init(TestGate *gate, bool open) {
    memset(gate, 0, sizeof(*gate));
    gate->open = open;
    assert(pthread_mutex_init(&gate->mutex, NULL) == 0);
    assert(pthread_cond_init(&gate->cond, NULL) == 0);
}

static void gate_destroy(TestGate *gate) {
    pthread_cond_destroy(&gate->cond);
    pthread_mutex_destroy(&gate->mutex);
}

static void gate_wait_started(TestGate *gate, int target) {
    pthread_mutex_lock(&gate->mutex);
    while (gate->started < target) {
        pthread_cond_wait(&gate->cond, &gate->mutex);
    }
    pthread_mutex_unlock(&gate->mutex);
}

static void gate_release(TestGate *gate) {
    pthread_mutex_lock(&gate->mutex);
    gate->open = true;
    pthread_cond_broadcast(&gate->cond);
    pthread_mutex_unlock(&gate->mutex);
}

static void gate_wait_completed(TestGate *gate, int target) {
    pthread_mutex_lock(&gate->mutex);
    while (gate->completed < target) {
        pthread_cond_wait(&gate->cond, &gate->mutex);
    }
    pthread_mutex_unlock(&gate->mutex);
}

static bool gated_echo_handler(const WorkerMessage *job, WorkerMessage *result, void *context) {
    TestGate *gate = context;
    pthread_mutex_lock(&gate->mutex);
    ++gate->started;
    pthread_cond_broadcast(&gate->cond);
    while (!gate->open) {
        pthread_cond_wait(&gate->cond, &gate->mutex);
    }
    ++gate->completed;
    pthread_cond_broadcast(&gate->cond);
    pthread_mutex_unlock(&gate->mutex);
    return worker_echo_handler(job, result, NULL);
}

static bool drop_if_marked_handler(const WorkerMessage *job, WorkerMessage *result, void *context) {
    (void)context;
    if (strcmp(job->body, "{\"drop\":1}") == 0) {
        return false;
    }
    return worker_echo_handler(job, result, NULL);
}

static PollRoute route_by_auth(const char *body, size_t body_size) {
    (void)body_size;
    return strstr(body, "\"AUTH\"") != NULL ? POLL_ROUTE_AUTH : POLL_ROUTE_BUSINESS;
}

static void pump_until_frame(PollServer *server, int client_fd, FrameInput *input) {
    struct pollfd client_poll = {.fd = client_fd, .events = POLLIN};
    for (int step = 0; step < 200; ++step) {
        assert(poll_server_step(server, monotonic_ms(), 10) == 0);
        if (poll(&client_poll, 1, 0) == 1 && (client_poll.revents & POLLIN)) {
            assert(receive_frame(client_fd, input) == 1);
            return;
        }
    }
    assert(!"Timed out waiting for response frame");
}

static void test_worker_validation_and_drop(void) {
    const int before = open_fd_count();
    Worker worker = {0};
    worker_stop(&worker);
    worker_destroy(&worker);

    assert(worker_init(&worker, 0, worker_echo_handler, NULL) == -1);
    assert(worker_init(&worker, 4, NULL, NULL) == -1);
    assert(worker_init(&worker, 1, drop_if_marked_handler, NULL) == 0);
    assert(worker_notify_fd(&worker) >= 0);

    WorkerMessage result;
    assert(!worker_take_result(&worker, &result));
    assert(worker_submit(&worker, 1, "{\"drop\":1}", 10));

    // Đợi drop xong để slot duy nhất giải phóng rồi nộp frame bình thường.
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (worker_submit(&worker, 2, "{\"ok\":1}", 8)) {
            break;
        }
        assert(attempt < 199);
        usleep(1000);
    }

    struct pollfd notify_poll = {.fd = worker_notify_fd(&worker), .events = POLLIN};
    assert(poll(&notify_poll, 1, 1000) == 1);
    worker_drain_notify(&worker);
    assert(worker_take_result(&worker, &result));
    assert(result.connection_id == 2 && strcmp(result.body, "{\"ok\":1}") == 0);
    free(result.body);
    assert(!worker_take_result(&worker, &result));

    worker_destroy(&worker);
    assert(open_fd_count() == before);
}

static void test_queue_full_returns_server_busy(void) {
    const int before = open_fd_count();
    const int listener = open_listener("127.0.0.1", 0);
    const uint16_t port = (uint16_t)local_port(listener);
    assert(listener >= 0 && port > 0);

    TestGate gate;
    gate_init(&gate, false);
    Worker business_worker = {0};
    assert(worker_init(&business_worker, 2, gated_echo_handler, &gate) == 0);

    static PollServer server;
    assert(poll_server_init(&server, listener, poll_default_timeouts()) == 0);
    server.business_worker = &business_worker;

    const int c1 = connect_to_server("127.0.0.1", port);
    const int c2 = connect_to_server("127.0.0.1", port);
    const int c3 = connect_to_server("127.0.0.1", port);
    assert(c1 >= 0 && c2 >= 0 && c3 >= 0);
    while (server.num_connections < 3) {
        assert(poll_server_step(&server, monotonic_ms(), 50) == 0);
    }

    // c1 chạy trên worker và dừng ở gate; c2 lấp đầy slot còn lại; c3 nhận SERVER_BUSY.
    assert(send_frame(c1, "{\"x\":1}", 7) == 0);
    while (!server.connections[0].request_active) {
        assert(poll_server_step(&server, monotonic_ms(), 50) == 0);
    }
    gate_wait_started(&gate, 1);

    assert(send_frame(c2, "{\"x\":2}", 7) == 0);
    while (!server.connections[1].request_active) {
        assert(poll_server_step(&server, monotonic_ms(), 50) == 0);
    }

    assert(send_frame(c3, "{\"x\":3}", 7) == 0);
    FrameInput input = {0};
    pump_until_frame(&server, c3, &input);
    assert(strstr(input.reader.body, "\"SERVER_BUSY\"") != NULL);

    gate_release(&gate);
    pump_until_frame(&server, c1, &input);
    assert(strcmp(input.reader.body, "{\"x\":1}") == 0);
    pump_until_frame(&server, c2, &input);
    assert(strcmp(input.reader.body, "{\"x\":2}") == 0);
    frame_reset(&input.reader);

    assert(close(c1) == 0 && close(c2) == 0 && close(c3) == 0);
    poll_server_stop(&server);
    worker_destroy(&business_worker);
    poll_server_destroy(&server);
    gate_destroy(&gate);
    assert(close(listener) == 0 && open_fd_count() == before);
}

static void test_nonblocking_io_stale_completion_and_auth_route(void) {
    const int before = open_fd_count();
    const int listener = open_listener("127.0.0.1", 0);
    const uint16_t port = (uint16_t)local_port(listener);
    assert(listener >= 0 && port > 0);

    TestGate business_gate;
    TestGate auth_gate;
    gate_init(&business_gate, false);
    gate_init(&auth_gate, false);

    Worker business_worker = {0};
    Worker auth_worker = {0};
    assert(worker_init(&business_worker, WORKER_BUSINESS_QUEUE_SIZE,
                       gated_echo_handler, &business_gate) == 0);
    assert(worker_init(&auth_worker, WORKER_AUTH_QUEUE_SIZE,
                       gated_echo_handler, &auth_gate) == 0);

    static PollServer server;
    assert(poll_server_init(&server, listener, poll_default_timeouts()) == 0);
    server.business_worker = &business_worker;
    server.auth_worker = &auth_worker;
    server.route_request = route_by_auth;

    // 1. slow_client giữ business_worker bị chặn tại business_gate.
    const int slow_client = connect_to_server("127.0.0.1", port);
    assert(slow_client >= 0);
    while (server.num_connections < 1) {
        assert(poll_server_step(&server, monotonic_ms(), 50) == 0);
    }
    assert(send_frame(slow_client, "{\"slow\":1}", 10) == 0);
    while (!server.connections[0].request_active) {
        assert(poll_server_step(&server, monotonic_ms(), 50) == 0);
    }
    gate_wait_started(&business_gate, 1);

    // 2. Trong lúc business_worker bận, auth_worker vẫn xử lý fast_client với ngân sách 45s.
    const int fast_client = connect_to_server("127.0.0.1", port);
    assert(fast_client >= 0);
    while (server.num_connections < 2) {
        assert(poll_server_step(&server, monotonic_ms(), 50) == 0);
    }
    assert(send_frame(fast_client, "{\"AUTH\":1}", 10) == 0);
    while (!server.connections[1].auth_job_active) {
        assert(poll_server_step(&server, monotonic_ms(), 50) == 0);
    }
    assert(server.connections[1].auth_deadline_ms > monotonic_ms() + 30000);

    gate_release(&auth_gate);
    gate_wait_completed(&auth_gate, 1);
    FrameInput input = {0};
    pump_until_frame(&server, fast_client, &input);
    assert(strcmp(input.reader.body, "{\"AUTH\":1}") == 0);

    // 3. Đóng đột ngột slow_client (RST) rồi mở reused_client; kết quả cũ trả về phải bị bỏ qua.
    const struct linger rst_linger = {.l_onoff = 1, .l_linger = 0};
    assert(setsockopt(slow_client, SOL_SOCKET, SO_LINGER,
                      &rst_linger, sizeof(rst_linger)) == 0);
    assert(close(slow_client) == 0);
    while (server.num_connections > 1) {
        assert(poll_server_step(&server, monotonic_ms(), 50) == 0);
    }
    const int reused_client = connect_to_server("127.0.0.1", port);
    assert(reused_client >= 0);
    while (server.num_connections < 2) {
        assert(poll_server_step(&server, monotonic_ms(), 50) == 0);
    }

    gate_release(&business_gate);
    gate_wait_completed(&business_gate, 1);
    struct pollfd stale_pipe = {.fd = worker_notify_fd(&business_worker), .events = POLLIN};
    assert(poll(&stale_pipe, 1, 1000) == 1);
    assert(poll_server_step(&server, monotonic_ms(), 0) == 0);

    assert(send_frame(reused_client, "{\"AUTH\":2}", 10) == 0);
    pump_until_frame(&server, reused_client, &input);
    assert(strcmp(input.reader.body, "{\"AUTH\":2}") == 0);
    frame_reset(&input.reader);

    assert(close(fast_client) == 0 && close(reused_client) == 0);
    poll_server_stop(&server);
    worker_destroy(&auth_worker);
    worker_destroy(&business_worker);
    poll_server_destroy(&server);
    gate_destroy(&business_gate);
    gate_destroy(&auth_gate);
    assert(close(listener) == 0 && open_fd_count() == before);
}

static void test_shutdown_finishes_active_and_cancels_queued(void) {
    const int before = open_fd_count();
    TestGate gate;
    gate_init(&gate, false);

    Worker worker = {0};
    assert(worker_init(&worker, 4, gated_echo_handler, &gate) == 0);
    assert(worker_submit(&worker, 1, "{\"j\":1}", 7));
    gate_wait_started(&gate, 1);
    assert(worker_submit(&worker, 2, "{\"j\":2}", 7));
    assert(worker_submit(&worker, 3, "{\"j\":3}", 7));

    worker_stop(&worker);
    assert(!worker_submit(&worker, 4, "{\"j\":4}", 7));
    gate_release(&gate);
    worker_destroy(&worker);

    assert(gate.completed == 1);
    gate_destroy(&gate);
    assert(open_fd_count() == before);
}

int main(void) {
    test_worker_validation_and_drop();
    test_queue_full_returns_server_busy();
    test_nonblocking_io_stale_completion_and_auth_route();
    test_shutdown_finishes_active_and_cancels_queued();
    return 0;
}
