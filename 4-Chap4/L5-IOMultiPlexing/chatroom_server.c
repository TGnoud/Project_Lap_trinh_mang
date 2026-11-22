#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

#define PORT 8080
#define MAX_CLIENTS 50

int send_message(int client_socket, const char *message, int length) {
    int sent = 0;
    while (sent < length) {
        int n = send(client_socket, message + sent, length - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return -1;
        }
        sent += n;
    }
    return 0;
}

int main(void) {
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener < 0) {
        perror("socket");
        return 1;
    }
    if (listener >= FD_SETSIZE) {
        fprintf(stderr, "Socket vuot gioi han cua select().\n");
        close(listener);
        return 1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listener, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(listener);
        return 1;
    }
    if (listen(listener, 5) < 0) {
        perror("listen");
        close(listener);
        return 1;
    }

    int clients[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i] = -1;
    }

    printf("Chatroom server dang lang nghe tai cong %d...\n", PORT);

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listener, &read_fds);
        int max_fd = listener;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] != -1) {
                FD_SET(clients[i], &read_fds);
                if (clients[i] > max_fd) {
                    max_fd = clients[i];
                }
            }
        }

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int client_socket = clients[i];
            if (client_socket == -1 || !FD_ISSET(client_socket, &read_fds)) {
                continue;
            }

            char buffer[1024];
            int received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
            if (received < 0 && errno == EINTR) {
                continue;
            }
            if (received <= 0) {
                printf("Client %d da ngat ket noi.\n", client_socket);
                close(client_socket);
                clients[i] = -1;
                continue;
            }

            buffer[received] = '\0';
            char message[1100];
            int length = snprintf(message, sizeof(message),
                                  "Client %d: %s", client_socket, buffer);
            printf("%s", message);
            fflush(stdout);

            for (int j = 0; j < MAX_CLIENTS; j++) {
                if (clients[j] != -1 && j != i) {
                    if (send_message(clients[j], message, length) < 0) {
                        printf("Client %d khong nhan duoc tin.\n", clients[j]);
                        close(clients[j]);
                        clients[j] = -1;
                    }
                }
            }
        }

        if (FD_ISSET(listener, &read_fds)) {
            int client_socket = accept(listener, NULL, NULL);
            if (client_socket < 0) {
                perror("accept");
                continue;
            }

            int slot = -1;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i] == -1) {
                    slot = i;
                    break;
                }
            }

            if (slot == -1 || client_socket >= FD_SETSIZE) {
                printf("Khong the nhan them client.\n");
                close(client_socket);
            } else {
                clients[slot] = client_socket;
                printf("Client %d da ket noi.\n", client_socket);
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != -1) {
            close(clients[i]);
        }
    }
    close(listener);
    return 1;
}
