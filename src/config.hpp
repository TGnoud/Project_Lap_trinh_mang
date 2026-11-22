#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace places {

enum class App { server, client };

struct Config {
    std::string host = "127.0.0.1";
    std::uint16_t port = 9000;
    std::string db_path;
    bool help = false;
    bool check_config = false;
};

Config parse_config(App app, const std::vector<std::string_view>& arguments);
int run_bootstrap(App app, int argc, char* argv[]);

}  // namespace places
