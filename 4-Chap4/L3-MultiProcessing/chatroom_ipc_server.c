#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 8080
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

int main(void)
{
    signal(SIGPIPE, SIG_IGN); /* Do not stop if a client has disconnected. */
    signal(SIGCHLD, SIG_IGN); /* Reap child processes automatically. */

    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == -1) {
        perror("socket");
        return 1;
    }

    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server, (struct sockaddr *)&address, sizeof(address)) == -1 ||
        listen(server, MAX_CLIENTS) == -1) {
        perror("bind/listen");
        close(server);
        return 1;
    }

    int clients[MAX_CLIENTS], pipes[MAX_CLIENTS];
    int count = 0;
    printf("Chatroom listening on port %d\n", PORT);

    while (1) {
        fd_set reads;
        FD_ZERO(&reads);
        FD_SET(server, &reads);
        int max_fd = server;

        for (int i = 0; i < count; i++) {
            FD_SET(pipes[i], &reads);
            if (pipes[i] > max_fd) max_fd = pipes[i];
        }

        if (select(max_fd + 1, &reads, NULL, NULL, NULL) == -1) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (FD_ISSET(server, &reads)) {
            int client = accept(server, NULL, NULL);
            if (client >= 0) {
                if (count == MAX_CLIENTS) {
                    const char *full = "Chatroom is full\n";
                    send(client, full, strlen(full), 0);
                    close(client);
                } else {
                    int channel[2];
                    if (pipe(channel) == -1) {
                        perror("pipe");
                        close(client);
                    } else {
                        pid_t child = fork();
                        if (child == 0) {
                            close(server);
                            close(channel[0]);
                            for (int i = 0; i < count; i++) {
                                close(clients[i]);
                                close(pipes[i]);
                            }

                            char buffer[BUFFER_SIZE];
                            ssize_t n;
                            while ((n = recv(client, buffer, sizeof(buffer), 0)) > 0)
                                write(channel[1], buffer, (size_t)n);

                            close(client);
                            close(channel[1]);
                            _exit(0);
                        }

                        close(channel[1]);
                        if (child == -1) {
                            perror("fork");
                            close(channel[0]);
                            close(client);
                        } else {
                            clients[count] = client;
                            pipes[count] = channel[0];
                            count++;
                            printf("Client connected (%d/%d)\n", count, MAX_CLIENTS);
                        }
                    }
                }
            }
        }

        /* Each pipe belongs to one sender, so skip that client when broadcasting. */
        for (int i = 0; i < count;) {
            if (!FD_ISSET(pipes[i], &reads)) {
                i++;
                continue;
            }

            char message[BUFFER_SIZE];
            ssize_t n = read(pipes[i], message, sizeof(message));
            if (n > 0) {
                for (int j = 0; j < count; j++)
                    if (j != i) send(clients[j], message, (size_t)n, 0);
                i++;
            } else {
                close(pipes[i]);
                close(clients[i]);
                for (int j = i; j < count - 1; j++) {
                    clients[j] = clients[j + 1];
                    pipes[j] = pipes[j + 1];
                }
                count--;
                printf("Client disconnected (%d/%d)\n", count, MAX_CLIENTS);
            }
        }
    }

    close(server);
    return 0;
}
