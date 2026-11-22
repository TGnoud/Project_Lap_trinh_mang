#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define PORT 8080
#define BUF_SIZE 8192

/* Gửi toàn bộ dữ liệu */
void send_all(int sock, const void *data, size_t len) {
    const char *p = data;

    while (len > 0) {
        ssize_t n = send(sock, p, len, 0);
        if (n <= 0)
            return;

        p += n;
        len -= n;
    }
}

/* 404 */
void send_404(int client) {
    const char *body = "404 Not Found";

    char header[256];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        strlen(body));

    send_all(client, header, n);
    send_all(client, body, strlen(body));
}

/* Hiển thị thư mục */
void list_directory(int client, const char *fs_path, const char *url_path) {
    DIR *dir = opendir(fs_path);

    if (!dir) {
        send_404(client);
        return;
    }

    char body[BUF_SIZE * 4];
    int len = 0;

    len += snprintf(body + len, sizeof(body) - len,
        "<html><body>"
        "<h1>Directory: %s</h1>"
        "<ul>",
        url_path);

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        char full_path[1024];

        if (strcmp(fs_path, ".") == 0)
            snprintf(full_path, sizeof(full_path),
                     "./%s", entry->d_name);
        else
            snprintf(full_path, sizeof(full_path),
                     "%s/%s", fs_path, entry->d_name);

        struct stat st;

        if (stat(full_path, &st) == -1)
            continue;

        char link[1024];

        if (strcmp(url_path, "/") == 0)
            snprintf(link, sizeof(link),
                     "/%s", entry->d_name);
        else
            snprintf(link, sizeof(link),
                     "%s/%s", url_path, entry->d_name);

        len += snprintf(body + len, sizeof(body) - len,
            "<li><a href=\"%s\">%s%s</a></li>",
            link,
            entry->d_name,
            S_ISDIR(st.st_mode) ? "/" : "");
    }

    closedir(dir);

    len += snprintf(body + len, sizeof(body) - len,
        "</ul></body></html>");

    char header[256];

    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        len);

    send_all(client, header, hlen);
    send_all(client, body, len);
}

/* Download file */
void send_file(int client, const char *path) {
    int fd = open(path, O_RDONLY);

    if (fd < 0) {
        send_404(client);
        return;
    }

    struct stat st;
    fstat(fd, &st);

    const char *filename = strrchr(path, '/');

    if (filename)
        filename++;
    else
        filename = path;

    char header[512];

    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Disposition: attachment; filename=\"%s\"\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        filename,
        (long)st.st_size);

    send_all(client, header, hlen);

    char buffer[BUF_SIZE];
    ssize_t n;

    while ((n = read(fd, buffer, sizeof(buffer))) > 0)
        send_all(client, buffer, n);

    close(fd);
}

/* Xử lý request */
void handle_client(int client) {
    char request[BUF_SIZE];

    int n = recv(client, request, sizeof(request) - 1, 0);

    if (n <= 0)
        return;

    request[n] = '\0';

    char method[16];
    char url[1024];

    if (sscanf(request, "%15s %1023s", method, url) != 2)
        return;

    /* Chỉ hỗ trợ GET */
    if (strcmp(method, "GET") != 0)
        return;

    /*
     * Không cho:
     * /../secret
     * /folder/../../secret
     */
    if (strstr(url, "..") != NULL) {
        send_404(client);
        return;
    }

    char path[1200];

    if (strcmp(url, "/") == 0)
        strcpy(path, ".");
    else
        snprintf(path, sizeof(path), ".%s", url);

    struct stat st;

    if (stat(path, &st) == -1) {
        send_404(client);
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        list_directory(client, path, url);
    }
    else if (S_ISREG(st.st_mode)) {
        send_file(client, path);
    }
    else {
        send_404(client);
    }
}

int main() {
    int server = socket(AF_INET, SOCK_STREAM, 0);

    if (server < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               &opt, sizeof(opt));

    struct sockaddr_in addr = {0};

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server,
             (struct sockaddr *)&addr,
             sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(server, 10) < 0) {
        perror("listen");
        return 1;
    }

    printf("Server running at http://localhost:%d/\n", PORT);

    while (1) {
        int client = accept(server, NULL, NULL);

        if (client < 0)
            continue;

        handle_client(client);

        close(client);
    }

    close(server);
    return 0;
}