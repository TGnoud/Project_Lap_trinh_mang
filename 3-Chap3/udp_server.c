#include <arpa/inet.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define SERVER_PORT 9090
#define MUSIC_DIR "./music"
#define BLOCK_SIZE (1024 * 10)

static int is_wav_file(const char *name)
{
    size_t length = strlen(name);
    return length >= 4 && strcmp(name + length - 4, ".wav") == 0;
}

static int valid_filename(const char *name)
{
    return is_wav_file(name) &&
           strchr(name, '/') == NULL &&
           strchr(name, '\\') == NULL;
}

static void send_text(int socket_fd, const struct sockaddr_in *client,
                      const char *text)
{
    sendto(socket_fd, text, strlen(text), 0,
           (const struct sockaddr *)client, sizeof(*client));
}

static void send_song_list(int socket_fd, const struct sockaddr_in *client)
{
    DIR *music_dir = opendir(MUSIC_DIR);
    if (music_dir == NULL)
    {
        send_text(socket_fd, client, "ERROR: cannot open music directory\n");
        return;
    }

    /* The list is returned in one UDP datagram. */
    char list[60000] = "";
    size_t used = 0;
    struct dirent *entry;

    while ((entry = readdir(music_dir)) != NULL)
    {
        if (!is_wav_file(entry->d_name))
            continue;

        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", MUSIC_DIR, entry->d_name);

        struct stat info;
        if (stat(path, &info) != 0 || !S_ISREG(info.st_mode))
            continue;

        int added = snprintf(list + used, sizeof(list) - used,
                             "%s\n", entry->d_name);
        if (added < 0 || (size_t)added >= sizeof(list) - used)
        {
            closedir(music_dir);
            send_text(socket_fd, client, "ERROR: song list is too large\n");
            return;
        }
        used += (size_t)added;
    }

    closedir(music_dir);

    if (used == 0)
        strcpy(list, "No .wav files found\n");

    sendto(socket_fd, list, strlen(list), 0,
           (const struct sockaddr *)client, sizeof(*client));
}

static void send_song(int socket_fd, const struct sockaddr_in *client,
                      const char *filename)
{
    if (!valid_filename(filename))
    {
        send_text(socket_fd, client, "ERROR: expected a .wav filename\n");
        return;
    }

    char path[4096];
    int path_length = snprintf(path, sizeof(path), "%s/%s",
                               MUSIC_DIR, filename);
    if (path_length < 0 || (size_t)path_length >= sizeof(path))
    {
        send_text(socket_fd, client, "ERROR: filename is too long\n");
        return;
    }

    FILE *song = fopen(path, "rb");
    if (song == NULL)
    {
        send_text(socket_fd, client, "ERROR: song not found\n");
        return;
    }

    /* Tell the client that the file exists before sending its binary data. */
    send_text(socket_fd, client, "OK");

    char block[BLOCK_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(block, 1, sizeof(block), song)) > 0)
    {
        ssize_t bytes_sent = sendto(socket_fd, block, bytes_read, 0,
                                    (const struct sockaddr *)client,
                                    sizeof(*client));
        if (bytes_sent < 0 || (size_t)bytes_sent != bytes_read)
        {
            perror("sendto file block");
            fclose(song);
            return;
        }
    }

    if (ferror(song))
    {
        perror("read song file");
        fclose(song);
        return;
    }

    fclose(song);

    /* A zero-length UDP datagram marks the end of the file. */
    if (sendto(socket_fd, "", 0, 0,
               (const struct sockaddr *)client, sizeof(*client)) < 0)
    {
        perror("sendto end marker");
        return;
    }

    printf("Sent %s to %s:%u\n", filename, inet_ntoa(client->sin_addr),
           (unsigned)ntohs(client->sin_port));
}

int main(void)
{
    int server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_socket < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_socket, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_socket);
        return EXIT_FAILURE;
    }

    printf("UDP music server listening on port %d\n", SERVER_PORT);

    for (;;)
    {
        char request[512];
        struct sockaddr_in client_addr;
        socklen_t client_length = sizeof(client_addr);

        ssize_t received = recvfrom(server_socket, request,
                                    sizeof(request) - 1, 0,
                                    (struct sockaddr *)&client_addr,
                                    &client_length);
        if (received < 0)
        {
            perror("recvfrom");
            continue;
        }

        request[received] = '\0';
        request[strcspn(request, "\r\n")] = '\0';

        if (strcmp(request, "GETLIST") == 0)
        {
            send_song_list(server_socket, &client_addr);
        }
        else if (strncmp(request, "DOWNLOAD ", 9) == 0)
        {
            send_song(server_socket, &client_addr, request + 9);
        }
        else
        {
            send_text(server_socket, &client_addr,
                      "ERROR: use GETLIST or DOWNLOAD <file.wav>\n");
        }
    }
}
