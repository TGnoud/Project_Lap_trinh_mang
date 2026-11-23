#ifndef PLACES_CONFIG_H
#define PLACES_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { APP_SERVER, APP_CLIENT } App;

typedef struct {
    const char *host;
    uint16_t port;
    const char *db_path;
    bool help;
    bool check_config;
} Config;

/* host and db_path refer to argv strings or to string literals. */
int parse_config(App app, int argc, char *const argv[], Config *config);
int read_config(App app, int argc, char *const argv[], Config *config);

#endif
