cmake_minimum_required(VERSION 3.16)
project(render_harness C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(ZLIB REQUIRED)

enable_testing()

# Real epdiy rendering sources, compiled unchanged (drawing logic is pure
# software; the hardware layer they call into is stubbed in epd_stub.c).
set(EPDIY_RENDER_SOURCES
    ${REPO_ROOT}/components/epdiy2/src/epdiy.c
    ${REPO_ROOT}/components/epdiy2/src/font.c
    ${REPO_ROOT}/components/epdiy2/src/displays.c
    ${REPO_ROOT}/components/epdiy2/src/builtin_waveforms.c
)
# Third-party driver code: keep our warning flags scoped to harness sources.
set_source_files_properties(${EPDIY_RENDER_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

add_executable(harness_tests
    tests/harness_tests.c
    src/png_writer.c
    src/epd_stub.c
    src/miniz_shim.c
    ${EPDIY_RENDER_SOURCES}
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

add_executable(render_harness
    src/harness_main.c
    src/epd_stub.c
    src/png_writer.c
    src/miniz_shim.c
    ${EPDIY_RENDER_SOURCES}
    ${REPO_ROOT}/main/display_screen.c
    ${REPO_ROOT}/main/display_viewport.c
    ${REPO_ROOT}/main/display_font.c
    ${REPO_ROOT}/main/text_utils.c
)
target_include_directories(render_harness PRIVATE
    src
    src/esp_shim
    ${REPO_ROOT}/main
    ${REPO_ROOT}/components/epdiy2/src
)
target_link_libraries(render_harness PRIVATE ZLIB::ZLIB)
target_compile_options(render_harness PRIVATE -Wall -Wextra)

find_package(Freetype)
if(FREETYPE_FOUND)
    target_link_libraries(render_harness PRIVATE Freetype::Freetype)
else()
    message(WARNING "FreeType not found; SD fonts disabled, built-in font fallback only")
endif()

add_test(
    NAME render_smoke
    COMMAND render_harness
        --sdcard ${CMAKE_CURRENT_SOURCE_DIR}/assets/sdcard
        --out ${CMAKE_CURRENT_BINARY_DIR}/smoke.png
)
