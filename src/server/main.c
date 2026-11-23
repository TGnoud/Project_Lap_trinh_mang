#include "config.h"
#include "transport/poll_loop.h"
#include "transport/tcp.h"

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested = 0;

static void handle_stop_signal(int signal_number) {
    (void)signal_number;
    stop_requested = 1;
}

int main(int argc, char *argv[]) {
    Config config;
    const int status = read_config(APP_SERVER, argc, argv, &config);
    if (status != 0 || config.help || config.check_config) {
        return status;
    }

    const int listener = open_listener(config.host, config.port);
    if (listener == -1) {
        return 1;
    }
    const int port = local_port(listener);
    static PollServer server;
    if (port == -1 || poll_server_init(&server, listener, poll_default_timeouts()) == -1) {
        close(listener);
        return 1;
    }

    struct sigaction action = {0};
    action.sa_handler = handle_stop_signal;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);

    printf("Listening on %s:%d\n", config.host, port);
    fflush(stdout);

    // Chạy vòng poll() cho nhiều client như bài 4-Chap4/chat_room_server.c.
    int exit_code = 0;
    while (!stop_requested) {
        if (poll_server_step(&server, monotonic_ms(), -1) == -1) {
            exit_code = 1;
            break;
        }
    }
    poll_server_stop(&server);
    poll_server_destroy(&server);
    close(listener);
    return exit_code;
}
