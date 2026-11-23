#include "config.h"
#include "transport/tcp.h"

#include <sys/socket.h>
#include <unistd.h>

#include <stdio.h>

int main(int argc, char* argv[]) {
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
    if (port == -1) {
        close(listener);
        return 1;
    }
    printf("Listening on %s:%d\n", config.host, port);
    fflush(stdout);

    // Nhận một kết nối rồi đóng hai fd như bài 2-Chap2/tcp_server.c.
    const int client = accept(listener, NULL, NULL);
    if (client == -1) {
        perror("accept");
        close(listener);
        return 1;
    }
    puts("Client connected");

    close(client);
    close(listener);
    return 0;
}
