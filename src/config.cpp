#include "config.hpp"

#include <charconv>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace places {
namespace {

std::uint16_t parse_port(std::string_view value, App app) {
    unsigned port = 0;
    const auto end = value.data() + value.size();
    const auto result = std::from_chars(value.data(), end, port);
    if (result.ec != std::errc{} || result.ptr != end ||
        port > std::numeric_limits<std::uint16_t>::max() ||
        (port == 0 && app == App::client)) {
        throw std::invalid_argument(app == App::server
            ? "--port must be an integer from 0 to 65535"
            : "--port must be an integer from 1 to 65535");
    }
    return static_cast<std::uint16_t>(port);
}

}  // namespace

Config parse_config(App app, const std::vector<std::string_view>& arguments) {
    Config config;
    const bool server = app == App::server;
    if (server) {
        config.db_path = "places.db";
    }

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto option = arguments[index];
        if (option == "--help") {
            config.help = true;
        } else if (option == "--check-config") {
            config.check_config = true;
        } else if (option == "--host" || option == "--port" || (server && option == "--db")) {
            ++index;
            if (index == arguments.size() || arguments[index].empty() ||
                arguments[index].substr(0, 2) == "--") {
                throw std::invalid_argument("missing value for " + std::string(option));
            }
            const auto value = arguments[index];
            if (option == "--host") {
                config.host = value;
            } else if (option == "--port") {
                config.port = parse_port(value, app);
            } else {
                config.db_path = value;
            }
        } else {
            throw std::invalid_argument("unknown option: " + std::string(option));
        }
    }
    return config;
}

int run_bootstrap(App app, int argc, char* argv[]) {
    const bool server = app == App::server;
    const char* name = server ? "place_server" : "place_cli";
    const std::vector<std::string_view> arguments(argv + 1, argv + argc);
    try {
        const auto config = parse_config(app, arguments);
        if (config.help) {
            std::cout << "Usage: " << name << " [--host HOST] [--port PORT]";
            if (server) {
                std::cout << " [--db PATH]";
            }
            std::cout << " [--check-config] [--help]\n";
            return 0;
        }
        if (config.check_config) {
            std::cout << "host=" << config.host << " port=" << config.port;
            if (server) {
                std::cout << " db=" << config.db_path;
            }
            std::cout << '\n';
            return 0;
        }
        std::cerr << name << ": TCP startup is not available yet; use --check-config.\n";
        return 1;
    } catch (const std::invalid_argument& error) {
        std::cerr << name << ": " << error.what() << '\n';
        return 2;
    }
}

}  // namespace places
