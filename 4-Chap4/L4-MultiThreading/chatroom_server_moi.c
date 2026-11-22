#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8080
#define MAX_CLIENTS 100

int clients[MAX_CLIENTS];
int number_of_clients = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void add_client(int client_socket) {
    clients[number_of_clients] = client_socket;
    number_of_clients++;
    printf("Client %d da ket noi. So client: %d\n", client_socket, number_of_clients);
}

void remove_client(int client_socket) {
    for (int i = 0; i < number_of_clients; i++) {
        if (clients[i] == client_socket) {
            for (int j = i; j < number_of_clients - 1; j++) {
                clients[j] = clients[j + 1];
            }
            number_of_clients--;
            printf("Client %d da roi. So client: %d\n", client_socket, number_of_clients);
            break;
        }
    }
}

void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);
    char buffer[1024];
    char message[1100];

    while (1) {
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            break;
        }
        buffer[bytes_received] = '\0';
        int length = snprintf(message, sizeof(message), "Client %d: %s", client_socket, buffer);

        pthread_mutex_lock(&lock);
        printf("%s", message);
        fflush(stdout);
        for (int i = 0; i < number_of_clients; i++) {
            if (clients[i] != client_socket) {
                int sent = 0;
                while (sent < length) {
                    int n = send(clients[i], message + sent, length - sent, MSG_NOSIGNAL);
                    if (n <= 0) break;
                    sent += n;
                }
            }
        }
        pthread_mutex_unlock(&lock);
    }

    pthread_mutex_lock(&lock);
    remove_client(client_socket);
    close(client_socket);
    pthread_mutex_unlock(&lock);
    return NULL;
}

int main(void) {
    int listener_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener_socket < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listener_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0 ||
        listen(listener_socket, 5) < 0) {
        perror("bind/listen");
        close(listener_socket);
        return 1;
    }
    printf("Server dang lang nghe tai cong %d...\n", PORT);
    fflush(stdout);

    while (1) {
        int client_socket = accept(listener_socket, NULL, NULL);
        if (client_socket < 0) {
            perror("accept");
            continue;
        }

        int *client_arg = malloc(sizeof(int));
        if (client_arg == NULL) {
            close(client_socket);
            continue;
        }
        *client_arg = client_socket;

        pthread_mutex_lock(&lock);
        if (number_of_clients == MAX_CLIENTS) {
            pthread_mutex_unlock(&lock);
            printf("Phong chat da day.\n");
            free(client_arg);
            close(client_socket);
            continue;
        }
        add_client(client_socket);
        pthread_mutex_unlock(&lock);

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, client_arg) != 0) {
            pthread_mutex_lock(&lock);
            remove_client(client_socket);
            close(client_socket);
            pthread_mutex_unlock(&lock);
            free(client_arg);
            continue;
        }
        pthread_detach(thread_id);
    }

    close(listener_socket);
    return 0;
}
