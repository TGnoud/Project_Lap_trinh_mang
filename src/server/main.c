#include "config.h"
#include "transport/poll_server.h"
#include "transport/tcp.h"

#include <unistd.h>

int main(int argc, char* argv[]) {
    Config config;
    const int status = read_config(APP_SERVER, argc, argv, &config);
    if (status != 0 || config.help || config.check_config) {
        return status;
    }

    const int listener = open_listener(config.host, config.port);
    if (listener == -1) {
        return 1;
    }
    const int port = local_port(listener);
    if (port == -1) {
        close(listener);
        return 1;
    }
    const int result = run_poll_server(listener, config.host, port);
    close(listener);
    return result;
}
