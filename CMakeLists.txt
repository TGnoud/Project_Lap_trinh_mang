cmake_minimum_required(VERSION 3.22)
project(it4062_places LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Build this project on Linux or WSL2 Ubuntu.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
option(ENABLE_COVERAGE "Collect GCC/gcov coverage" OFF)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(ENABLE_COVERAGE AND ENABLE_SANITIZERS)
    message(FATAL_ERROR "Collect coverage and run sanitizers in separate builds.")
endif()

add_library(place_build_options INTERFACE)
target_compile_options(place_build_options INTERFACE -Wall -Wextra -Wpedantic)

if(ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "ENABLE_COVERAGE requires GCC and the matching gcov version.")
    endif()
    target_compile_options(place_build_options INTERFACE --coverage -O0 -g)
    target_link_options(place_build_options INTERFACE --coverage)
elseif(ENABLE_SANITIZERS)
    target_compile_options(place_build_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(place_build_options INTERFACE -fsanitize=address,undefined)
endif()

add_library(place_config STATIC src/config.cpp)
target_include_directories(place_config PUBLIC src)
target_link_libraries(place_config PUBLIC place_build_options)

add_executable(place_server src/server/main.cpp)
target_link_libraries(place_server PRIVATE place_config)

add_executable(place_cli src/client/main.cpp)
target_link_libraries(place_cli PRIVATE place_config)

if(BUILD_TESTING)
    find_package(GTest 1.14 CONFIG QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        if(POLICY CMP0135)
            cmake_policy(SET CMP0135 NEW)
        endif()
        FetchContent_Declare(
            googletest
            URL https://codeload.github.com/google/googletest/tar.gz/refs/tags/v1.16.0
            URL_HASH SHA256=78c676fc63881529bf97bf9d45948d905a66833fbfa5318ea2cd7478cb98f399
        )
        set(INSTALL_GTEST OFF)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(place_config_test tests/unit/config_test.cpp)
    target_link_libraries(place_config_test PRIVATE place_config GTest::gtest_main)
    add_test(NAME config.unit COMMAND place_config_test)
    set_tests_properties(config.unit PROPERTIES LABELS "w1;unit")

    add_test(NAME config.server COMMAND place_server --check-config --port 0 --db sample.db)
    set_tests_properties(config.server PROPERTIES LABELS w1
        PASS_REGULAR_EXPRESSION "host=127[.]0[.]0[.]1 port=0 db=sample[.]db")
    add_test(NAME config.cli COMMAND place_cli --check-config --host localhost --port 8080)
    set_tests_properties(config.cli PROPERTIES LABELS w1
        PASS_REGULAR_EXPRESSION "host=localhost port=8080")

    add_test(NAME config.server_help COMMAND place_server --help)
    add_test(NAME config.cli_help COMMAND place_cli --help)
    set_tests_properties(config.server_help config.cli_help PROPERTIES LABELS w1)

    add_test(NAME config.invalid_cli COMMAND place_cli --check-config --port 0)
    add_test(NAME config.no_mode COMMAND place_server)
    set_tests_properties(config.invalid_cli config.no_mode PROPERTIES LABELS w1 WILL_FAIL TRUE)
endif()
