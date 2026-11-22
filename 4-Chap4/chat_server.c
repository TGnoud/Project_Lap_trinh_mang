#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>

typedef struct client_list{
    int client;
    char nickname[512];
    int joined; // 0 for not joined, 1 for joined
    struct client_list *next;
} client_list;


client_list *client_head = NULL;
void add_client(int client_socket);
void remove_client(int client_socket);
void join_room(int client_socket, const char *nickname);
char *get_nickname(int client_socket);
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
void *handle_client(void *arg);
static int send_message(int client_socket, const char *message);
int main(){
    // Create a socket
    int server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY;
   if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        exit(1);
    }
    listen(server_socket, 5);
    printf("Server is listening on port 8080...\n");

    while (1) {
        // Accept a client connection
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        int *arg = malloc(sizeof(*arg));
        if (arg == NULL) {
            close(client_socket);
            continue;
        }
        *arg = client_socket;
        pthread_mutex_lock(&lock);
        add_client(client_socket);
        pthread_mutex_unlock(&lock);
        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client, arg) != 0) {
            pthread_mutex_lock(&lock);
            remove_client(client_socket);
            pthread_mutex_unlock(&lock);
            close(client_socket);
            free(arg);
            continue;
        }
        pthread_detach(thread);
    }
    return 0;
}

void *handle_client(void *arg){
    int client_socket = *(int *)arg;
    free(arg);
    char input[1024];
    char buffer[1024];
    char response[sizeof(buffer) + sizeof(client_head->nickname)];
    ssize_t bytes_received = 0;
    ssize_t next_byte = 0;
    size_t command_length = 0;
    while (1) {
        if (next_byte == bytes_received) {
            do {
                bytes_received = recv(client_socket, input, sizeof(input), 0);
            } while (bytes_received < 0 && errno == EINTR);
            if (bytes_received <= 0) break;
            next_byte = 0;
        }

        if (command_length == sizeof(buffer) - 1) {
            send_message(client_socket, "ERROR: Command too long.\n");
            break;
        }
        char received = input[next_byte++];
        buffer[command_length++] = received;
        if (received != '\n') continue;
        buffer[command_length] = '\0';
        command_length = 0;

        if (strncmp(buffer, "JOIN ", 5) == 0) {
            char *nickname = buffer + 5;
            nickname[strcspn(nickname, "\r\n")] = 0; // Remove newline characters
            join_room(client_socket, nickname);
            snprintf(response, sizeof(response), "Welcome %s!\n", nickname);
            send_message(client_socket, response);
        } else if (strncmp(buffer, "MSG ", 4) == 0) {
            // send message to all clients
            char *message = buffer + 4;
            printf("Received message from client %d: %s\n", client_socket, message);
            
            snprintf(response, sizeof(response), "%s: %s", get_nickname(client_socket), message);

            pthread_mutex_lock(&lock);
            for (client_list *current = client_head; current != NULL; current = current->next) {
                if (current->client != client_socket && current->joined == 1) {
                    send_message(current->client, response);
                }
            }
            pthread_mutex_unlock(&lock);

        } else if (strncmp(buffer, "PMSG", 4) == 0 && strchr(" \t\r\n", buffer[4]) != NULL) {
            char *nickname = buffer + 4;
            nickname[strcspn(nickname, "\r\n")] = '\0';
            while (*nickname == ' ' || *nickname == '\t') nickname++;

            char *content = nickname + strcspn(nickname, " \t");
            if (*content != '\0') {
                *content++ = '\0';
                while (*content == ' ' || *content == '\t') content++;
            }
            if (*nickname == '\0' || *content == '\0') {
                send_message(client_socket, "ERROR: Usage: PMSG <nickname> <message>\n");
                continue;
            }

            const char *sender_name = NULL;
            client_list *recipient = NULL;
            int matches = 0;
            const char *error = NULL;
            pthread_mutex_lock(&lock);
            for (client_list *current = client_head; current != NULL; current = current->next) {
                if (current->joined != 1) continue;
                if (current->client == client_socket) {
                    sender_name = current->nickname;
                } else if (strcmp(current->nickname, nickname) == 0) {
                    recipient = current;
                    matches++;
                }
            }
            if (sender_name == NULL) {
                error = "ERROR: JOIN before using PMSG.\n";
            } else if (matches == 0) {
                error = "ERROR: Recipient not found.\n";
            } else if (matches > 1) {
                error = "ERROR: Recipient nickname is ambiguous.\n";
            } else {
                snprintf(response, sizeof(response), "%s: %s\n", sender_name, content);
                if (send_message(recipient->client, response) < 0) {
                    error = "ERROR: Could not send private message.\n";
                }
            }
            pthread_mutex_unlock(&lock);
            if (error != NULL) send_message(client_socket, error);
        } else if (strncmp(buffer, "QUIT", 4) == 0) {
            snprintf(response, sizeof(response), "Goodbye %s!\n", get_nickname(client_socket));
            send_message(client_socket, response);
            break;
        } else {
            snprintf(response, sizeof(response), "Unknown command: %s\n", buffer);
            send_message(client_socket, response);
        }
    }

    pthread_mutex_lock(&lock);
    remove_client(client_socket);
    pthread_mutex_unlock(&lock);

    close(client_socket);
    
    return NULL;
}

static int send_message(int client_socket, const char *message) {
    size_t length = strlen(message);
    size_t sent = 0;
    while (sent < length) {
        ssize_t n = send(client_socket, message + sent, length - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

void add_client(int client_socket){
    client_list *new_client = (client_list *)malloc(sizeof(client_list));
    new_client->client = client_socket;
    new_client->nickname[0] = '\0';
    new_client->joined = 0;
    new_client->next = client_head;
    
    client_head = new_client;
    while(new_client->next != NULL){
        printf("[%d] -> ", new_client->client);
        new_client = new_client->next;  
    }
    printf("[%d]\n", new_client->client);
}


void remove_client(int client_socket){
    if(client_head == NULL) return;

    if(client_head->client == client_socket){
        client_list *temp = client_head;
        client_head = client_head->next;
        free(temp);
        return;
    }

    client_list *current = client_head;
    while(current->next != NULL && current->next->client != client_socket){
        current = current->next;
    }

    if(current->next != NULL){
        client_list *temp = current->next;
        current->next = current->next->next;
        free(temp);
    }
    
    while(current != NULL){
        printf("[%d] -> ", current->client);
        current = current->next;  
    }
}

void join_room(int client_socket, const char *nickname){
    pthread_mutex_lock(&lock);
    client_list *current = client_head;
    while(current != NULL){
        if(current->client == client_socket){
            strncpy(current->nickname, nickname, sizeof(current->nickname) - 1);
            current->nickname[sizeof(current->nickname) - 1] = '\0'; // Ensure null-termination
            current->joined = 1;
            break;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&lock);
}

char* get_nickname(int client_socket){
    pthread_mutex_lock(&lock);
    client_list *current = client_head;
    while(current != NULL){
        if(current->client == client_socket){
            pthread_mutex_unlock(&lock);
            return current->nickname;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&lock);
    return NULL; // Return NULL if the client is not found
}
