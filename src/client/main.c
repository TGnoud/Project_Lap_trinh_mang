#include "config.h"
#include "transport/tcp.h"

#include <unistd.h>

#include <stdio.h>

int main(int argc, char* argv[]) {
    Config config;
    const int status = read_config(APP_CLIENT, argc, argv, &config);
    if (status != 0 || config.help || config.check_config) {
        return status;
    }

    const int client = connect_to_server(config.host, config.port);
    if (client == -1) {
        return 1;
    }
    printf("Connected to %s:%u\n", config.host, (unsigned)config.port);
    close(client);
    return 0;
}
