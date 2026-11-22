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

int main()
{
    // Tạo Socket UDP
    int client_socket = socket(
        AF_INET, 
        SOCK_DGRAM, 
        IPPROTO_UDP
    );
    // Server socket address
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9090);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    char buffer[1024] = {0};
    while (0 == 0){
        printf("Enter message to send: ");
        fgets(buffer, sizeof(buffer), stdin);
        while (buffer[strlen(buffer) - 1] == '\n' || buffer[strlen(buffer) - 1] == '\r') {
            buffer[strlen(buffer) - 1] = '\0';
        }
        size_t sent_bytes = sendto(
            client_socket,
            buffer,
            strlen(buffer),
            0,
            (struct sockaddr *)&addr,
            sizeof(addr)
        );
        if (sent_bytes == -1) {
            perror("sendto");
            break; 
        }
        if (strcmp(buffer, "GETLIST") == 0){
            while(0==0){
                memset(buffer, 0, sizeof(buffer));
                ssize_t rev_bytes = recvfrom(
                    client_socket,
                    buffer,
                    sizeof(buffer),
                    0,
                    NULL,
                    NULL
                );
                if (rev_bytes == -1) {
                    perror("recvfrom");
                    break;
                }
                if (strcmp(buffer, "END_OF_LIST") == 0) {
                    break;
                }
                printf("Received: %s\n", buffer);
            }
        } else if (strncmp(buffer, "DOWNLOAD ", 9) == 0) {
            char filename[256];
            strncpy(filename, buffer + 9, sizeof(filename) - 1);
            filename[sizeof(filename) - 1] = '\0'; // Ensure null-termination
            char filepath[256];
            snprintf(filepath, sizeof(filepath), "%s", filename);
            FILE *file = fopen(filepath, "wb");
            if (file == NULL) {
                perror("fopen");
                break;
            }
            while (0==0){
                memset(buffer, 0, sizeof(buffer));
                ssize_t rev_bytes = recvfrom(
                    client_socket,
                    buffer,
                    sizeof(buffer),
                    0,
                    NULL,
                    NULL
                );
                if (rev_bytes == -1) {
                    perror("recvfrom");
                    break;
                }
                if (rev_bytes == (ssize_t)strlen("END_OF_FILE") &&
                    memcmp(buffer, "END_OF_FILE", rev_bytes) == 0) {
                    break;
                }
                fwrite(buffer, 1, (size_t)rev_bytes, file);
            }
            fclose(file);
        }

    }

}
