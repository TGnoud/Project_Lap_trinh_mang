#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9090
#define BLOCK_SIZE (1024 * 10)
#define WAIT_TIMEOUT_MS 5000

static int wait_for_data(int socket_fd)
{
    struct pollfd event = {0};
    event.fd = socket_fd;
    event.events = POLLIN;

    int result = poll(&event, 1, WAIT_TIMEOUT_MS);
    if (result == 0)
    {
        fprintf(stderr, "Timed out waiting for the server\n");
        return -1;
    }
    if (result < 0)
    {
        perror("poll");
        return -1;
    }
    return 0;
}

static int valid_filename(const char *name)
{
    size_t length = strlen(name);
    return length >= 4 &&
           strcmp(name + length - 4, ".wav") == 0 &&
           strchr(name, '/') == NULL &&
           strchr(name, '\\') == NULL;
}

int main(void)
{
    int client_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (client_socket < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) != 1)
    {
        fprintf(stderr, "Invalid server IP address\n");
        close(client_socket);
        return EXIT_FAILURE;
    }

    char command[512];
    printf("Enter command (GETLIST or DOWNLOAD <file.wav>): ");
    fflush(stdout);

    if (fgets(command, sizeof(command), stdin) == NULL)
    {
        close(client_socket);
        return EXIT_FAILURE;
    }
    command[strcspn(command, "\r\n")] = '\0';

    int is_list = strcmp(command, "GETLIST") == 0;
    const char *filename = NULL;

    if (!is_list && strncmp(command, "DOWNLOAD ", 9) == 0)
    {
        filename = command + 9;
        if (!valid_filename(filename))
        {
            fprintf(stderr, "Use DOWNLOAD followed by a .wav filename\n");
            close(client_socket);
            return EXIT_FAILURE;
        }
    }
    else if (!is_list)
    {
        fprintf(stderr, "Unknown command\n");
        close(client_socket);
        return EXIT_FAILURE;
    }

    if (sendto(client_socket, command, strlen(command), 0,
               (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("sendto");
        close(client_socket);
        return EXIT_FAILURE;
    }

    if (wait_for_data(client_socket) < 0)
    {
        close(client_socket);
        return EXIT_FAILURE;
    }

    if (is_list)
    {
        char list[65536];
        ssize_t received = recvfrom(client_socket, list, sizeof(list) - 1, 0,
                                    NULL, NULL);
        if (received < 0)
        {
            perror("recvfrom");
            close(client_socket);
            return EXIT_FAILURE;
        }

        list[received] = '\0';
        if (strncmp(list, "ERROR:", 6) == 0)
        {
            fprintf(stderr, "%s", list);
            close(client_socket);
            return EXIT_FAILURE;
        }

        printf("%s", list);
        close(client_socket);
        return EXIT_SUCCESS;
    }

    char status[128];
    ssize_t status_length = recvfrom(client_socket, status,
                                     sizeof(status) - 1, 0, NULL, NULL);
    if (status_length < 0)
    {
        perror("recvfrom");
        close(client_socket);
        return EXIT_FAILURE;
    }
    status[status_length] = '\0';

    if (strncmp(status, "ERROR:", 6) == 0)
    {
        fprintf(stderr, "%s", status);
        close(client_socket);
        return EXIT_FAILURE;
    }
    if (strcmp(status, "OK") != 0)
    {
        fprintf(stderr, "Unexpected response from server\n");
        close(client_socket);
        return EXIT_FAILURE;
    }

    FILE *output = fopen(filename, "wb");
    if (output == NULL)
    {
        perror("fopen");
        close(client_socket);
        return EXIT_FAILURE;
    }

    char block[BLOCK_SIZE];
    for (;;)
    {
        if (wait_for_data(client_socket) < 0)
        {
            fclose(output);
            remove(filename);
            close(client_socket);
            return EXIT_FAILURE;
        }

        ssize_t received = recvfrom(client_socket, block, sizeof(block), 0,
                                    NULL, NULL);
        if (received < 0)
        {
            perror("recvfrom");
            fclose(output);
            remove(filename);
            close(client_socket);
            return EXIT_FAILURE;
        }

        if (received == 0)
            break;

        if (fwrite(block, 1, (size_t)received, output) != (size_t)received)
        {
            perror("fwrite");
            fclose(output);
            remove(filename);
            close(client_socket);
            return EXIT_FAILURE;
        }
    }

    fclose(output);
    printf("Downloaded %s to the current directory\n", filename);

    close(client_socket);
    return EXIT_SUCCESS;
}
