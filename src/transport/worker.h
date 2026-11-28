#ifndef PLACES_WORKER_H
#define PLACES_WORKER_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    WORKER_BUSINESS_QUEUE_SIZE = 256,
    WORKER_AUTH_QUEUE_SIZE = 128,
    WORKER_AUTH_BUDGET_MS = 45000,
};

typedef struct {
    uint64_t connection_id;
    char *body;
    size_t body_size;
} WorkerMessage;

/* Worker copies response into result->body (malloc) and returns true if a response is ready. */
typedef bool (*WorkerHandler)(const WorkerMessage *job, WorkerMessage *result, void *context);

typedef struct {
    WorkerMessage *items;
    size_t capacity;
    size_t head;
    size_t count;
} WorkerRing;

typedef struct {
    int pipe_fd[2];
    size_t capacity;
    size_t pending_total;
    bool stopping;
    bool started;
    WorkerHandler handler;
    void *context;
    WorkerRing jobs;
    WorkerRing results;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;
} Worker;

int worker_init(Worker *worker, size_t capacity, WorkerHandler handler, void *context);
void worker_stop(Worker *worker);
void worker_destroy(Worker *worker);

bool worker_submit(Worker *worker, uint64_t connection_id, const char *body, size_t body_size);
int worker_notify_fd(const Worker *worker);
void worker_drain_notify(Worker *worker);
bool worker_take_result(Worker *worker, WorkerMessage *result);

/* Default D05 handler that echoes the frame body on the worker thread. */
bool worker_echo_handler(const WorkerMessage *job, WorkerMessage *result, void *context);

#endif
