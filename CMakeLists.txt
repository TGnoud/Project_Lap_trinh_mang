cmake_minimum_required(VERSION 3.22)
project(it4062_places LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Build this project on Linux or WSL2 Ubuntu.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

include(CTest)
option(ENABLE_COVERAGE "Collect GCC/gcov coverage" OFF)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(ENABLE_COVERAGE AND ENABLE_SANITIZERS)
    message(FATAL_ERROR "Collect coverage and run sanitizers in separate builds.")
endif()

add_library(place_build_options INTERFACE)
target_compile_options(place_build_options INTERFACE -Wall -Wextra -Wpedantic)

if(ENABLE_COVERAGE)
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "ENABLE_COVERAGE requires GCC and the matching gcov version.")
    endif()
    target_compile_options(place_build_options INTERFACE --coverage -O0 -g)
    target_link_options(place_build_options INTERFACE --coverage)
elseif(ENABLE_SANITIZERS)
    target_compile_options(place_build_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(place_build_options INTERFACE -fsanitize=address,undefined)
endif()

find_package(json-c CONFIG REQUIRED)
add_library(place_frame STATIC src/protocol/frame.c)
target_include_directories(place_frame PUBLIC src)
target_link_libraries(place_frame PUBLIC place_build_options PRIVATE json-c::json-c)

add_library(place_config STATIC src/config.c)
target_include_directories(place_config PUBLIC src)
target_link_libraries(place_config PUBLIC place_build_options)

add_library(place_transport STATIC
    src/transport/tcp.c
    src/transport/frame_io.c
    src/transport/poll_loop.c)
target_include_directories(place_transport PUBLIC src)
target_link_libraries(place_transport PUBLIC place_build_options place_frame)

add_executable(place_server src/server/main.c)
target_link_libraries(place_server PRIVATE place_config place_transport)

add_executable(place_cli src/client/main.c)
target_link_libraries(place_cli PRIVATE place_config place_transport)

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    add_executable(place_config_test tests/unit/config_test.c)
    target_link_libraries(place_config_test PRIVATE place_config)
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
    set_tests_properties(config.invalid_cli PROPERTIES LABELS w1 WILL_FAIL TRUE)

    add_executable(place_transport_test tests/unit/tcp_test.c)
    target_link_libraries(place_transport_test PRIVATE place_transport)
    add_test(NAME transport.unit COMMAND place_transport_test)
    set_tests_properties(transport.unit PROPERTIES LABELS "w1;unit" TIMEOUT 10)

    add_test(NAME transport.demo COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tcp/socket_demo.py"
        "$<TARGET_FILE:place_server>" "$<TARGET_FILE:place_cli>")
    set_tests_properties(transport.demo PROPERTIES LABELS w1 TIMEOUT 20)

    add_executable(place_frame_test tests/unit/frame_test.c)
    target_link_libraries(place_frame_test PRIVATE place_frame)
    add_test(NAME frame.unit COMMAND place_frame_test)
    set_tests_properties(frame.unit PROPERTIES LABELS "w1;unit")

    add_test(NAME frame.tcp COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tcp/framing_cases.py" tcp
        "$<TARGET_FILE:place_server>" "$<TARGET_FILE:place_cli>")
    add_test(NAME frame.contract COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tcp/framing_cases.py" contract
        "$<TARGET_FILE:place_server>")
    set_tests_properties(frame.tcp frame.contract PROPERTIES LABELS w1 TIMEOUT 30)

    add_test(NAME transport.poll COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tcp/poll_cases.py"
        "$<TARGET_FILE:place_server>")
    set_tests_properties(transport.poll PROPERTIES LABELS w1 TIMEOUT 30)
endif()
