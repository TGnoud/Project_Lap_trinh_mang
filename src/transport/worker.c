#include "transport/worker.h"
#include "transport/tcp.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool ring_init(WorkerRing *ring, size_t capacity) {
    ring->items = calloc(capacity, sizeof(*ring->items));
    if (ring->items == NULL) {
        return false;
    }
    ring->capacity = capacity;
    ring->head = 0;
    ring->count = 0;
    return true;
}

static void ring_push(WorkerRing *ring, WorkerMessage item) {
    const size_t tail = (ring->head + ring->count) % ring->capacity;
    ring->items[tail] = item;
    ++ring->count;
}

static void ring_pop(WorkerRing *ring, WorkerMessage *item) {
    *item = ring->items[ring->head];
    ring->head = (ring->head + 1) % ring->capacity;
    --ring->count;
}

static void ring_destroy(WorkerRing *ring) {
    while (ring->count > 0) {
        WorkerMessage item;
        ring_pop(ring, &item);
        free(item.body);
    }
    free(ring->items);
    *ring = (WorkerRing){0};
}

// Báo hoàn tất về vòng poll() qua pipe non-blocking như bài 4-Chap4/L3-MultiProcessing/chatroom_ipc_server.c.
static void notify_reactor(int write_fd) {
    const unsigned char signal_byte = 1;
    for (;;) {
        const ssize_t written = write(write_fd, &signal_byte, 1);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        return;
    }
}

// Luồng worker hữu hạn dùng pthread và mutex như bài 4-Chap4/L4-MultiThreading/ban_ve.c.
static void *worker_main(void *argument) {
    Worker *worker = argument;
    pthread_mutex_lock(&worker->mutex);
    for (;;) {
        while (worker->jobs.count == 0 && !worker->stopping) {
            pthread_cond_wait(&worker->cond, &worker->mutex);
        }
        if (worker->stopping) {
            break;
        }

        WorkerMessage job;
        ring_pop(&worker->jobs, &job);
        pthread_mutex_unlock(&worker->mutex);

        WorkerMessage result = {.connection_id = job.connection_id};
        const bool produced = worker->handler(&job, &result, worker->context);
        free(job.body);

        pthread_mutex_lock(&worker->mutex);
        if (produced) {
            ring_push(&worker->results, result);
            pthread_mutex_unlock(&worker->mutex);
            notify_reactor(worker->pipe_fd[1]);
            pthread_mutex_lock(&worker->mutex);
        } else {
            --worker->pending_total;
        }
    }
    pthread_mutex_unlock(&worker->mutex);
    return NULL;
}

int worker_init(Worker *worker, size_t capacity, WorkerHandler handler, void *context) {
    memset(worker, 0, sizeof(*worker));
    worker->pipe_fd[0] = -1;
    worker->pipe_fd[1] = -1;
    if (capacity == 0 || handler == NULL) {
        return -1;
    }
    if (pipe(worker->pipe_fd) == -1 || set_nonblocking(worker->pipe_fd[0]) == -1 ||
        set_nonblocking(worker->pipe_fd[1]) == -1) {
        if (worker->pipe_fd[0] >= 0) close(worker->pipe_fd[0]);
        if (worker->pipe_fd[1] >= 0) close(worker->pipe_fd[1]);
        return -1;
    }

    worker->capacity = capacity;
    worker->handler = handler;
    worker->context = context;
    if (!ring_init(&worker->jobs, capacity) || !ring_init(&worker->results, capacity) ||
        pthread_mutex_init(&worker->mutex, NULL) != 0 ||
        pthread_cond_init(&worker->cond, NULL) != 0 ||
        pthread_create(&worker->thread, NULL, worker_main, worker) != 0) {
        ring_destroy(&worker->jobs);
        ring_destroy(&worker->results);
        close(worker->pipe_fd[0]);
        close(worker->pipe_fd[1]);
        return -1;
    }
    worker->started = true;
    return 0;
}

void worker_stop(Worker *worker) {
    if (!worker->started) {
        return;
    }
    pthread_mutex_lock(&worker->mutex);
    worker->stopping = true;
    pthread_cond_broadcast(&worker->cond);
    pthread_mutex_unlock(&worker->mutex);
}

void worker_destroy(Worker *worker) {
    if (!worker->started) {
        return;
    }
    worker_stop(worker);
    pthread_join(worker->thread, NULL);
    ring_destroy(&worker->jobs);
    ring_destroy(&worker->results);
    close(worker->pipe_fd[0]);
    close(worker->pipe_fd[1]);
    pthread_cond_destroy(&worker->cond);
    pthread_mutex_destroy(&worker->mutex);
    worker->started = false;
}

bool worker_submit(Worker *worker, uint64_t connection_id, const char *body, size_t body_size) {
    char *copy = malloc(body_size + 1);
    if (copy == NULL) {
        return false;
    }
    memcpy(copy, body, body_size);
    copy[body_size] = '\0';

    pthread_mutex_lock(&worker->mutex);
    if (worker->stopping || worker->pending_total >= worker->capacity) {
        pthread_mutex_unlock(&worker->mutex);
        free(copy);
        return false;
    }
    ring_push(&worker->jobs, (WorkerMessage){
        .connection_id = connection_id,
        .body = copy,
        .body_size = body_size,
    });
    ++worker->pending_total;
    pthread_cond_signal(&worker->cond);
    pthread_mutex_unlock(&worker->mutex);
    return true;
}

int worker_notify_fd(const Worker *worker) {
    return worker->pipe_fd[0];
}

void worker_drain_notify(Worker *worker) {
    unsigned char buffer[64];
    for (;;) {
        const ssize_t count = read(worker->pipe_fd[0], buffer, sizeof(buffer));
        if (count > 0 || (count < 0 && errno == EINTR)) {
            continue;
        }
        return;
    }
}

bool worker_take_result(Worker *worker, WorkerMessage *result) {
    pthread_mutex_lock(&worker->mutex);
    if (worker->results.count == 0) {
        pthread_mutex_unlock(&worker->mutex);
        return false;
    }
    ring_pop(&worker->results, result);
    --worker->pending_total;
    pthread_mutex_unlock(&worker->mutex);
    return true;
}

bool worker_echo_handler(const WorkerMessage *job, WorkerMessage *result, void *context) {
    (void)context;
    result->body = malloc(job->body_size + 1);
    if (result->body == NULL) {
        return false;
    }
    memcpy(result->body, job->body, job->body_size + 1);
    result->body_size = job->body_size;
    return true;
}
