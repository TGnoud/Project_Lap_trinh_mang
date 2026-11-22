#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8888
#define BUFFER_SIZE 1024

int main(void)
{
    int client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket == -1)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) != 1)
    {
        fprintf(stderr, "Invalid server address: %s\n", SERVER_IP);
        close(client_socket);
        return EXIT_FAILURE;
    }

    if (connect(client_socket, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) == -1)
    {
        perror("connect");
        close(client_socket);
        return EXIT_FAILURE;
    }

    /* Đọc và hiển thị lời chào do Telnet server gửi. */
    char buffer[BUFFER_SIZE];
    int got_newline = 0;
    while (!got_newline)
    {
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received == -1)
        {
            perror("recv");
            close(client_socket);
            return EXIT_FAILURE;
        }
        if (bytes_received == 0)
        {
            fprintf(stderr, "Server closed the connection before sending its greeting.\n");
            close(client_socket);
            return EXIT_FAILURE;
        }

        fwrite(buffer, 1, (size_t)bytes_received, stdout);
        if (memchr(buffer, '\n', (size_t)bytes_received) != NULL)
            got_newline = 1;
    }

    printf("Enter command: ");
    fflush(stdout);

    char command[BUFFER_SIZE];
    if (fgets(command, sizeof(command), stdin) == NULL)
    {
        fprintf(stderr, "No command was entered.\n");
        close(client_socket);
        return EXIT_FAILURE;
    }

    /* Gửi toàn bộ lệnh, kể cả khi send() chỉ gửi được một phần dữ liệu. */
    size_t command_length = strlen(command);
    size_t total_sent = 0;
    while (total_sent < command_length)
    {
        ssize_t bytes_sent = send(client_socket, command + total_sent,
                                  command_length - total_sent, 0);
        if (bytes_sent == -1)
        {
            if (errno == EINTR)
                continue;
            perror("send");
            close(client_socket);
            return EXIT_FAILURE;
        }
        if (bytes_sent == 0)
        {
            fprintf(stderr, "Connection closed while sending the command.\n");
            close(client_socket);
            return EXIT_FAILURE;
        }
        total_sent += (size_t)bytes_sent;
    }

    /* Nhận và in kết quả cho đến khi server đóng kết nối. */
    for (;;)
    {
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received == 0)
            break;
        if (bytes_received == -1)
        {
            if (errno == EINTR)
                continue;
            perror("recv");
            close(client_socket);
            return EXIT_FAILURE;
        }

        fwrite(buffer, 1, (size_t)bytes_received, stdout);
    }

    close(client_socket);
    return EXIT_SUCCESS;
}
