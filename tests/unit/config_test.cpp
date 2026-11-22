#include "config.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

using places::App;
using places::parse_config;

TEST(ConfigTest, DefaultsUseLoopback) {
    const auto server = parse_config(App::server, {});
    EXPECT_EQ(server.host, "127.0.0.1");
    EXPECT_EQ(server.port, 9000);
    EXPECT_EQ(server.db_path, "places.db");
    EXPECT_FALSE(server.help);
    EXPECT_FALSE(server.check_config);

    const auto client = parse_config(App::client, {});
    EXPECT_EQ(client.host, "127.0.0.1");
    EXPECT_EQ(client.port, 9000);
    EXPECT_TRUE(client.db_path.empty());
}

TEST(ConfigTest, AcceptsOverridesAndServerEphemeralPort) {
    const auto server = parse_config(App::server,
        {"--host", "192.0.2.10", "--port", "0", "--db", "test data/places.db", "--check-config"});
    EXPECT_EQ(server.host, "192.0.2.10");
    EXPECT_EQ(server.port, 0);
    EXPECT_EQ(server.db_path, "test data/places.db");
    EXPECT_TRUE(server.check_config);

    const auto client = parse_config(App::client, {"--port", "65535", "--help"});
    EXPECT_EQ(client.port, 65535);
    EXPECT_TRUE(client.help);
}

TEST(ConfigTest, RejectsInvalidPortValues) {
    for (const auto value : {"65536", "99999999999999", "-1", "1.5", " 80", "80x"}) {
        EXPECT_THROW(parse_config(App::server, {"--port", value}), std::invalid_argument) << value;
        EXPECT_THROW(parse_config(App::client, {"--port", value}), std::invalid_argument) << value;
    }
    EXPECT_THROW(parse_config(App::client, {"--port", "0"}), std::invalid_argument);
}

TEST(ConfigTest, RejectsMissingValuesAndUnknownOptions) {
    EXPECT_THROW(parse_config(App::server, {"--host"}), std::invalid_argument);
    EXPECT_THROW(parse_config(App::server, {"--host", ""}), std::invalid_argument);
    EXPECT_THROW(parse_config(App::server, {"--db", "--check-config"}), std::invalid_argument);
    EXPECT_THROW(parse_config(App::server, {"--unknown"}), std::invalid_argument);
    EXPECT_THROW(parse_config(App::client, {"--db", "other.db"}), std::invalid_argument);
}
