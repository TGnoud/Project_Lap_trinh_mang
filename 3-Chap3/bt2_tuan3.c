#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define SERVER_PORT 8081
#define BACKLOG 10
#define COMMAND_SIZE 1024
#define OUTPUT_SIZE 4096

static int send_all(int client_socket, const void *data, size_t length)
{
    const char *buffer = data;
    size_t sent = 0;

    while (sent < length)
    {
        ssize_t result = send(client_socket, buffer + sent, length - sent, 0);

        if (result > 0)
        {
            sent += (size_t)result;
        }
        else if (result == -1 && errno == EINTR)
        {
            continue;
        }
        else
        {
            return -1;
        }
    }

    return 0;
}

static int send_text(int client_socket, const char *text)
{
    return send_all(client_socket, text, strlen(text));
}

static void trim_command(char *command)
{
    char *start = command;
    char *end;

    while (isspace((unsigned char)*start))
        ++start;

    if (start != command)
        memmove(command, start, strlen(start) + 1);

    end = command + strlen(command);
    while (end > command && isspace((unsigned char)end[-1]))
        --end;
    *end = '\0';
}

static void handle_ls(int client_socket)
{
    DIR *directory = opendir(".");
    struct dirent *entry;
    int client_disconnected = 0;

    if (directory == NULL)
    {
        perror("opendir");
        (void)send_text(client_socket, "Cannot open current directory\n");
        return;
    }

    errno = 0;
    while ((entry = readdir(directory)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        if (send_text(client_socket, entry->d_name) == -1 ||
            send_text(client_socket, "\n") == -1)
        {
            client_disconnected = 1;
            break; 
        }
    }

    if (!client_disconnected && errno != 0)
        perror("readdir");

    closedir(directory);
}

static void handle_ifconfig(int client_socket)
{
    FILE *command_output;
    char buffer[OUTPUT_SIZE];
    size_t bytes_read;

    /* Chuoi lenh co dinh nen du lieu tu client khong the gay shell injection.
       2>&1 gui ca thong bao loi (neu co) ve client. */
    command_output = popen("ifconfig 2>&1", "r");
    if (command_output == NULL)
    {
        perror("popen");
        (void)send_text(client_socket, "Cannot execute ifconfig\n");
        return;
    }

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), command_output)) > 0)
    {
        if (send_all(client_socket, buffer, bytes_read) == -1)
            break; 
    }

    if (ferror(command_output))
        perror("fread ifconfig");

    (void)pclose(command_output);
}

static void handle_client(int client_socket)
{
    char command[COMMAND_SIZE];
    ssize_t bytes_received;

    do
    {
        bytes_received = recv(client_socket, command, sizeof(command) - 1, 0);
    } while (bytes_received == -1 && errno == EINTR);

    if (bytes_received == -1)
    {
        perror("recv");
        return;
    }

    if (bytes_received == 0)
        return; 

    command[bytes_received] = '\0';
    trim_command(command);
    printf("Received command: %s\n", command);

    if (strcmp(command, "ls") == 0)
        handle_ls(client_socket);
    else if (strcmp(command, "ifconfig") == 0)
        handle_ifconfig(client_socket);
    else
        (void)send_text(client_socket, "Invalid command\n");
}

int main(void)
{
    int listener;
    int reuse_address = 1;
    struct sockaddr_in server_address;

    /* Tranh server bi ket thuc neu client dong socket trong luc send(). */
    signal(SIGPIPE, SIG_IGN);

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                   sizeof(reuse_address)) == -1)
    {
        perror("setsockopt");
        close(listener);
        return EXIT_FAILURE;
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(SERVER_PORT);
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listener, (struct sockaddr *)&server_address,
             sizeof(server_address)) == -1)
    {
        perror("bind");
        close(listener);
        return EXIT_FAILURE;
    }

    if (listen(listener, BACKLOG) == -1)
    {
        perror("listen");
        close(listener);
        return EXIT_FAILURE;
    }

    printf("TCP Command Server is listening on port %d...\n", SERVER_PORT);

    /* Dong listener van giu nguyen; moi vong chi dong socket cua mot client. */
    for (;;)
    {
        int client_socket;

        do
        {
            client_socket = accept(listener, NULL, NULL);
        } while (client_socket == -1 && errno == EINTR);

        if (client_socket == -1)
        {
            perror("accept");
            continue;
        }

        handle_client(client_socket);
        close(client_socket);
    }
}
