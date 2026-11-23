#include "config.h"
#include "transport/frame_io.h"
#include "transport/tcp.h"

#include <unistd.h>

#include <stdio.h>
#include <string.h>

int main(int argc, char* argv[]) {
    Config config;
    const int status = read_config(APP_CLIENT, argc, argv, &config);
    if (status != 0 || config.help || config.check_config) {
        return status;
    }

    const int client = connect_to_server(config.host, config.port);
    if (client == -1) {
        return 1;
    }
    printf("Connected to %s:%u\n", config.host, (unsigned)config.port);

    // Gửi rồi nhận phản hồi như bài 2-Chap2/tcp_client.c.
    const char message[] = "{\"message\":\"Xin chào\"}";
    const size_t size = sizeof(message) - 1;
    if (send_frame(client, message, size) < 0) {
        perror("send");
        close(client);
        return 1;
    }

    FrameInput input = {0};
    const int received = receive_frame(client, &input);
    const bool matches = received == 1 && input.reader.body_size == size &&
                         memcmp(input.reader.body, message, size) == 0;
    if (matches) {
        printf("Echo: %s\n", input.reader.body);
    } else if (received >= 0) {
        fputs("Missing or mismatched echo frame\n", stderr);
    }
    frame_reset(&input.reader);
    close(client);
    return matches ? 0 : 1;
}
