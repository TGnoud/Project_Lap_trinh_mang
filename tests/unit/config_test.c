#include "config.h"

// Keep assertions active in both Debug and Release builds.
#undef NDEBUG
#include <assert.h>
#include <string.h>

#define ARG_COUNT(args) ((int)(sizeof(args) / sizeof((args)[0])))

static void test_defaults(void) {
    char *args[] = {"places"};
    Config config;
    assert(parse_config(APP_SERVER, ARG_COUNT(args), args, &config) == 0);
    assert(strcmp(config.host, "127.0.0.1") == 0);
    assert(config.port == 9000);
    assert(strcmp(config.db_path, "places.db") == 0);
    assert(!config.help && !config.check_config);

    assert(parse_config(APP_CLIENT, ARG_COUNT(args), args, &config) == 0);
    assert(strcmp(config.host, "127.0.0.1") == 0);
    assert(config.port == 9000);
    assert(config.db_path[0] == '\0');
}

static void test_overrides(void) {
    char *server_args[] = {"place_server", "--host", "192.0.2.10", "--port", "0",
                           "--db", "test data/places.db", "--check-config"};
    Config config;
    assert(parse_config(APP_SERVER, ARG_COUNT(server_args), server_args, &config) == 0);
    assert(strcmp(config.host, "192.0.2.10") == 0);
    assert(config.port == 0);
    assert(strcmp(config.db_path, "test data/places.db") == 0);
    assert(config.check_config);

    char *client_args[] = {"place_cli", "--port", "65535", "--help"};
    assert(parse_config(APP_CLIENT, ARG_COUNT(client_args), client_args, &config) == 0);
    assert(config.port == 65535);
    assert(config.help);
}

static void test_invalid_ports(void) {
    char *invalid[] = {"65536", "99999999999999", "1844674407370955161600", "-1", "+1", "1.5", " 80", "80x"};
    Config config;
    for (int i = 0; i < ARG_COUNT(invalid); ++i) {
        char *args[] = {"places", "--port", invalid[i]};
        assert(parse_config(APP_SERVER, ARG_COUNT(args), args, &config) == 2);
        assert(parse_config(APP_CLIENT, ARG_COUNT(args), args, &config) == 2);
    }
    char *zero[] = {"place_cli", "--port", "0"};
    assert(parse_config(APP_CLIENT, ARG_COUNT(zero), zero, &config) == 2);
}

static void test_invalid_options(void) {
    char *missing[] = {"places", "--host"};
    char *empty[] = {"places", "--host", ""};
    char *next_option[] = {"places", "--db", "--check-config"};
    char *unknown[] = {"places", "--unknown"};
    char *client_db[] = {"place_cli", "--db", "other.db"};
    Config config;
    assert(parse_config(APP_SERVER, ARG_COUNT(missing), missing, &config) == 2);
    assert(parse_config(APP_SERVER, ARG_COUNT(empty), empty, &config) == 2);
    assert(parse_config(APP_SERVER, ARG_COUNT(next_option), next_option, &config) == 2);
    assert(parse_config(APP_SERVER, ARG_COUNT(unknown), unknown, &config) == 2);
    assert(parse_config(APP_CLIENT, ARG_COUNT(client_db), client_db, &config) == 2);
}

int main(void) {
    test_defaults();
    test_overrides();
    test_invalid_ports();
    test_invalid_options();
    return 0;
}
