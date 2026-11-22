#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <poll.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>

#define BLOCK_SIZE 1024

static const char *find_playlist_path(void)
{
    static const char *paths[] = {"../music", "3-Chap3/music", "music"};
    struct stat info;

    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        if (stat(paths[i], &info) == 0 && S_ISDIR(info.st_mode)) {
            return paths[i];
        }
    }
    return paths[0];
}

int main()
{
    // Tạo Socket UDP
    int server_socket = socket(
        AF_INET, 
        SOCK_DGRAM, 
        IPPROTO_UDP
    );
    // Thiết lập địa chỉ và cổng cho socket
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9090);
    addr.sin_addr.s_addr = INADDR_ANY;
    // Liên kết socket với địa chỉ và cổng
    if (bind(
        server_socket, 
        (struct sockaddr *)&addr, 
        sizeof(addr)) == -1
    ) {
        perror("bind");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    // Thiết lập cấu trúc lưu đỉa socket của client
    struct sockaddr_in client_socket;
    socklen_t client_socket_len = sizeof(client_socket);

    printf("UDP Receiver is listening on port %d...\n", ntohs(addr.sin_port));

    const char *playlist_path = find_playlist_path();
    char buffer[BLOCK_SIZE] = {0};
    char block_send[BLOCK_SIZE] = {0};
    while (0 == 0){
        client_socket_len = sizeof(client_socket);
        ssize_t rev_bytes = recvfrom(
            server_socket, 
            buffer, 
            sizeof(buffer) - 1, 
            0, 
            (struct sockaddr *)&client_socket, 
            &client_socket_len
        );
        if (rev_bytes == -1) {
            perror("recvfrom");
            break;
        }
        buffer[rev_bytes] = '\0';
        while (rev_bytes > 0 &&
               (buffer[rev_bytes - 1] == '\n' || buffer[rev_bytes - 1] == '\r')) {
            buffer[rev_bytes - 1] = '\0';
            rev_bytes--;
        }
        printf("Received message: %s\n", buffer);
        if (strcmp(buffer, "GETLIST") == 0){
            DIR *playlist = opendir(playlist_path);
            if (playlist == NULL) {
                perror("opendir");
                const char *error_message = "ERROR: cannot open music directory";
                sendto(server_socket, error_message, strlen(error_message), 0,
                       (struct sockaddr *)&client_socket, client_socket_len);
            } else {
                struct dirent *entry;
                while ((entry = readdir(playlist)) != NULL) {
                    if (strcmp(entry->d_name, ".") == 0 ||
                        strcmp(entry->d_name, "..") == 0) {
                        continue;
                    }

                    char list_entry[NAME_MAX + 2];
                    int entry_length = snprintf(list_entry, sizeof(list_entry),
                                                "%s\n", entry->d_name);
                    if (entry_length < 0 || (size_t)entry_length >= sizeof(list_entry)) {
                        continue;
                    }
                    if (sendto(server_socket, list_entry, (size_t)entry_length, 0,
                               (struct sockaddr *)&client_socket,
                               client_socket_len) == -1) {
                        perror("sendto");
                        break;
                    }
                }
                closedir(playlist);
            }

            char *end_message = "END_OF_LIST";
            sendto(
                server_socket, 
                end_message, 
                strlen(end_message), 
                0, 
                (struct sockaddr *)&client_socket, 
                client_socket_len);
        } else if (strncmp(buffer, "DOWNLOAD ", 9) == 0) {
            char *filename = buffer + 9;
            char filepath[256];
            int path_length = snprintf(filepath, sizeof(filepath), "%s/%s",
                                       playlist_path, filename);
            if (path_length < 0 || (size_t)path_length >= sizeof(filepath)) {
                fprintf(stderr, "Requested file path is too long\n");
                continue;
            }
            FILE *fp = fopen(filepath, "rb");
            if (fp == NULL) {
                perror("fopen");
                break;
            }
            while (!feof(fp)){
                size_t read_bytes = fread(block_send, 1, sizeof(block_send), fp);
                if (read_bytes > 0) {
                    int send_bytes = sendto(
                        server_socket, 
                        block_send, 
                        read_bytes, 
                        0, 
                        (struct sockaddr *)&client_socket, 
                        client_socket_len
                    );
                }
            }
            char *end_message = "END_OF_FILE";
            sendto(
                server_socket, 
                end_message,
                strlen(end_message),
                0,
                (struct sockaddr *)&client_socket,
                client_socket_len
            );
            fclose(fp);
        }
    }
    
}
