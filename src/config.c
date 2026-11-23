#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_port(const char *value, App app, uint16_t *port) {
    char *end;
    errno = 0;
    unsigned long number = strtoul(value, &end, 10);
    if (value[0] < '0' || value[0] > '9' || *end != '\0' || errno == ERANGE ||
        number > UINT16_MAX || (number == 0 && app == APP_CLIENT)) {
        return false;
    }
    *port = (uint16_t)number;
    return true;
}

int parse_config(App app, int argc, char *const argv[], Config *config) {
    const bool server = app == APP_SERVER;
    const char *name = server ? "place_server" : "place_cli";
    *config = (Config){.host = "127.0.0.1", .port = 9000, .db_path = server ? "places.db" : ""};

    // Đọc argc/argv như bài 1-Chap1/lab04_pointer_to_pointer.c.
    for (int i = 1; i < argc; ++i) {
        const char *option = argv[i];
        if (strcmp(option, "--help") == 0) {
            config->help = true;
        } else if (strcmp(option, "--check-config") == 0) {
            config->check_config = true;
        } else if (strcmp(option, "--host") == 0 || strcmp(option, "--port") == 0 ||
                   (server && strcmp(option, "--db") == 0)) {
            ++i;
            if (i == argc || argv[i][0] == '\0' || strncmp(argv[i], "--", 2) == 0) {
                fprintf(stderr, "%s: missing value for %s\n", name, option);
                return 2;
            }
            if (strcmp(option, "--host") == 0) {
                config->host = argv[i];
            } else if (strcmp(option, "--db") == 0) {
                config->db_path = argv[i];
            } else if (!parse_port(argv[i], app, &config->port)) {
                fprintf(stderr, "%s: --port must be an integer from %d to 65535\n", name, server ? 0 : 1);
                return 2;
            }
        } else {
            fprintf(stderr, "%s: unknown option: %s\n", name, option);
            return 2;
        }
    }
    return 0;
}

int read_config(App app, int argc, char *const argv[], Config *config) {
    const int status = parse_config(app, argc, argv, config);
    if (status != 0) {
        return status;
    }

    const bool server = app == APP_SERVER;
    if (config->help) {
        printf("Usage: %s [--host HOST] [--port PORT]%s [--check-config] [--help]\n",
               server ? "place_server" : "place_cli", server ? " [--db PATH]" : "");
    } else if (config->check_config) {
        printf("host=%s port=%u", config->host, (unsigned)config->port);
        if (server) {
            printf(" db=%s", config->db_path);
        }
        putchar('\n');
    }
    return 0;
}
