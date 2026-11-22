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
    char nickname[50];
    struct client_list *next;
} client_list;

client_list *client_head = NULL;

void add_client(int client_socket);
void remove_client(int client_socket);
int set_nickname(int client_socket, char *nickname);
 // 100 OK, 200 NICKNAME IN USE, 300 INVALID NICKNAME, 999 UNKNOWN ERROR, 200, 300 yeu cau sinh vien thiet ke
char* get_nickname(int client_socket); // 100 OK, 999 UNKNOWN ERROR
void unset_nickname(int client_socket); // 100 OK, 999 UNKNOWN ERROR
void *handle_client(void *arg);
static int send_message(int client_socket, const char *message);
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;


int main(){
    // Create a socket
    int server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0 ||
        listen(server_socket, 5) < 0) {
        perror("chatroom");
        close(server_socket);
        return 1;
    }

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
    close(server_socket);
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
            send_message(client_socket, "999 COMMAND TOO LONG\r\n");
            break;
        }
        char received = input[next_byte++];
        buffer[command_length++] = received;
        // TCP có thể chia một lệnh thành nhiều lần recv hoặc gộp nhiều lệnh.
        if (received != '\n') continue;
        buffer[command_length] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';
        command_length = 0;

        if (strncmp(buffer, "JOIN ", 5) == 0){
            char *nickname = buffer + 5;
            pthread_mutex_lock(&lock);
            int state = set_nickname(client_socket, nickname);
            pthread_mutex_unlock(&lock);
            if (state == 100){
                send_message(client_socket, "100 OK\r\n");
            } else if (state == 200){
                send_message(client_socket, "200 NICKNAME IN USE\r\n");
            } else if (state == 300){
                send_message(client_socket, "300 INVALID NICKNAME\r\n");
            } else {
                send_message(client_socket, "999 UNKNOWN ERROR\r\n");
            }
        } else if (strncmp(buffer, "MSG ", 4) == 0){
            pthread_mutex_lock(&lock);
            char *client_nickname = get_nickname(client_socket);
            if (client_nickname != NULL) {
                snprintf(response, sizeof(response), "%s: %s\r\n", client_nickname, buffer + 4);
                for (client_list *current = client_head; current != NULL; current = current->next) {
                    if (current->client != client_socket && current->nickname[0] != '\0') {
                        send_message(current->client, response);
                    }
                }
            }
            pthread_mutex_unlock(&lock);
            send_message(client_socket, "100 OK\r\n");
        } else if (strncmp(buffer, "PMSG", 4) == 0 &&
                   (buffer[4] == ' ' || buffer[4] == '\t' || buffer[4] == '\0')) {
            char *nickname = buffer + 4;
            while (*nickname == ' ' || *nickname == '\t') nickname++;
            char *content = nickname + strcspn(nickname, " \t");
            if (*content != '\0') {
                *content++ = '\0';
                while (*content == ' ' || *content == '\t') content++;
            }
            if (*nickname == '\0' || *content == '\0') {
                send_message(client_socket, "999 INVALID PMSG\r\n");
                continue;
            }

            const char *status = "100 OK\r\n";
            pthread_mutex_lock(&lock);
            const char *sender = get_nickname(client_socket);
            client_list *recipient = NULL;
            int matches = 0;
            for (client_list *current = client_head; current != NULL; current = current->next) {
                if (current->client != client_socket && strcmp(current->nickname, nickname) == 0) {
                    recipient = current;
                    matches++;
                }
            }
            if (sender == NULL || *sender == '\0') {
                status = "999 JOIN REQUIRED\r\n";
            } else if (matches == 0) {
                status = "999 RECIPIENT NOT FOUND\r\n";
            } else if (matches > 1) {
                status = "200 NICKNAME IN USE\r\n";
            } else {
                snprintf(response, sizeof(response), "%s: %s\r\n", sender, content);
                if (send_message(recipient->client, response) < 0) {
                    status = "999 UNKNOWN ERROR\r\n";
                }
            }
            pthread_mutex_unlock(&lock);
            send_message(client_socket, status);
        } else if (strncmp(buffer, "QUIT", 4) == 0){
            pthread_mutex_lock(&lock);
            unset_nickname(client_socket);
            pthread_mutex_unlock(&lock);
            send_message(client_socket, "100 OK\r\n");
        } else {
            send_message(client_socket, "999 UNKNOWN COMMAND\r\n");
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
    new_client->next = client_head;
    new_client->nickname[0] = '\0'; // Initialize nickname to empty string
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

int set_nickname(int client_socket, char *nickname){
    client_list *current = client_head;
    while(current != NULL){
        if(current->client == client_socket){
            strncpy(current->nickname, nickname, sizeof(current->nickname) - 1);
            return 100; // OK   
        }
        current = current->next;
    }
    return 999; // UNKNOWN ERROR
}

char* get_nickname(int client_socket){
    client_list *current = client_head;
    while(current != NULL){
        if(current->client == client_socket){
            return current->nickname; // Return the nickname
        }
        current = current->next;
    }
    return NULL;
}

void unset_nickname(int client_socket){
    client_list *current = client_head;
    while(current != NULL){
        if(current->client == client_socket){
            current->nickname[0] = '\0'; // Clear the nickname
            return; // OK
        }
        current = current->next;
    }
}
