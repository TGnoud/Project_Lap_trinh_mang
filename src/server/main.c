#include "config.h"
#include "transport/frame_io.h"
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

    // Trả lại dữ liệu như bài 2-Chap2/tcp_server.c, sau khi nhận đủ một frame.
    FrameInput input = {0};
    int received;
    while ((received = receive_frame(client, &input)) == 1) {
        if (send_frame(client, input.reader.body, input.reader.body_size) < 0) {
            perror("send");
            received = -1;
            break;
        }
    }
    frame_reset(&input.reader);
    close(client);
    close(listener);
    return received == 0 ? 0 : 1;
}
