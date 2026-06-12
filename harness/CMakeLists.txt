cmake_minimum_required(VERSION 3.16)
project(render_harness C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(ZLIB REQUIRED)

enable_testing()

add_executable(harness_tests
    tests/harness_tests.c
    src/png_writer.c
    src/epd_stub.c
)
target_include_directories(harness_tests PRIVATE
    src
    src/esp_shim
    ${REPO_ROOT}/main
    ${REPO_ROOT}/components/epdiy2/src
)
target_link_libraries(harness_tests PRIVATE ZLIB::ZLIB)
target_compile_options(harness_tests PRIVATE -Wall -Wextra)
add_test(NAME harness_tests COMMAND harness_tests)
