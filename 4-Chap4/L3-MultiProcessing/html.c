#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <dirent.h>

void Append(char **output, const char *str)
{
    char *tmp = *output;
    int oldlen = tmp == NULL ? 0 : strlen(tmp);
    int newlen = oldlen + strlen(str) + 1;
    tmp = realloc(tmp, newlen);
    if (tmp == NULL)
    {
        perror("realloc() failed");
        exit(1);
    }
    tmp[newlen - 1] = 0;
    sprintf(tmp + oldlen, "%s", str);
    *output = tmp;
}

char *BuildDirectoryHTML(const char *path)
{
    DIR *dir = opendir(path);

    if (dir == NULL)
    {
        return NULL;
    }

    char *html = NULL;

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        char line[1024];

        if (entry->d_type == DT_DIR)
        {
            snprintf(
                line, sizeof(line),
                "<a href=\"%s/\">[DIR] %s</a><br>",
                entry->d_name,
                entry->d_name);
        }
        else
        {
            snprintf(
                line, sizeof(line),
                "<a href=\"%s?download=1\" download>%s</a><br>",
                entry->d_name,
                entry->d_name);
        }

        Append(&html, line);
    }

    closedir(dir);

    return html;
}

void ParseHTTPRequest(
    const char *buffer,
    char *method,
    char *endpoint)
{
    sscanf(buffer, "%15s %1023s", method, endpoint);
}

/* Thu hoi tat ca cac tien trinh con da ket thuc. */
void signal_handler(int signum)
{
    int saved_errno = errno;
    (void)signum;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
    errno = saved_errno;
}

/* send() co the chi gui mot phan du lieu, nen can gui tiep. */
int SendAll(int client, const char *data, size_t length)
{
    size_t sent = 0;
    while (sent < length)
    {
        ssize_t n = send(client, data + sent, length - sent, 0);
        if (n == -1 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        sent += n;
    }
    return 0;
}

/* Moi tien trinh con xu ly mot ket noi HTTP. */
void HandleClient(int client)
{
    char buffer[1024] = {0};
    char method[16] = {0};
    char endpoint[1024] = {0};

    int byte_received = recv(
        client,
        buffer,
        sizeof(buffer) - 1,
        0);

    if (byte_received <= 0)
    {
        if (byte_received == -1)
            perror("recv() failed");
        close(client);
        return;
    }

    ParseHTTPRequest(buffer, method, endpoint);

    printf("Received request: %s %s\n", method, endpoint);
    if (strstr(endpoint, "?download=1") != NULL)
    {
        char *query = strstr(endpoint, "?download=1");
        *query = '\0';

        // Giu cach dung duong dan tuyet doi cua code goc.
        char *file_path = endpoint;

        FILE *file = fopen(file_path, "rb");

        if (file == NULL)
        {
            perror("fopen() failed");

            const char *response =
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                "\r\n";

            SendAll(client, response, strlen(response));

            close(client);
            return;
        }

        // Lấy kích thước file
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        rewind(file);

        // Lấy tên file
        char *filename = strrchr(file_path, '/');

        if (filename == NULL)
            filename = file_path;
        else
            filename++;

        // Gửi HTTP header trước
        char header[2048];

        sprintf(
            header,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Disposition: attachment; filename=\"%s\"\r\n"
            "Content-Length: %ld\r\n"
            "Connection: close\r\n"
            "\r\n",
            filename,
            file_size);

        if (SendAll(client, header, strlen(header)) == -1)
        {
            fclose(file);
            close(client);
            return;
        }

        // Gửi nội dung file
        char file_buffer[1024];
        size_t bytes_read;

        while ((bytes_read = fread(
                    file_buffer,
                    1,
                    sizeof(file_buffer),
                    file)) > 0)
        {
            if (SendAll(client, file_buffer, bytes_read) == -1)
                break;
        }

        fclose(file);

        close(client);
        return;
    }
    else
    {
        char *content = NULL;

        Append(&content, "HTTP/1.1 200 OK\r\n");
        Append(&content, "Content-Type: text/html\r\n");
        Append(&content, "Connection: close\r\n");
        Append(&content, "\r\n");
        Append(&content, "<h1>Show server directory</h1>");
        Append(&content, "<p>This is a simple HTML page.</p>");
        char *directory_html = BuildDirectoryHTML(endpoint);
        if (directory_html != NULL)
        {
            Append(&content, directory_html);
            free(directory_html);
        }

        SendAll(client, content, strlen(content));
        free(content);
    }

    close(client);
}

int main(void)
{
    if (signal(SIGCHLD, signal_handler) == SIG_ERR ||
        signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    {
        perror("signal() failed");
        return 1;
    }

    int listener = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP);

    if (listener == -1)
    {
        perror("socket() failed");
        return 1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(
            listener,
            (struct sockaddr *)&addr,
            sizeof(addr)) == -1)
    {
        perror("bind() failed");
        close(listener);
        return 1;
    }

    if (listen(listener, 5) == -1)
    {
        perror("listen() failed");
        close(listener);
        return 1;
    }

    while (1)
    {
        int client = accept(listener, NULL, NULL);
        if (client == -1)
        {
            if (errno == EINTR)
                continue;
            perror("accept() failed");
            close(listener);
            return 1;
        }

        pid_t pid = fork();
        if (pid == -1)
        {
            perror("fork() failed");
            close(client);
            continue;
        }

        if (pid == 0)
        {
            close(listener);  // Con chi phuc vu client, khong nhan ket noi moi.
            HandleClient(client);
            exit(0);         // Con khong quay lai vong lap accept().
        }

        close(client);       // Cha de con so huu ket noi nay.
    }
}
