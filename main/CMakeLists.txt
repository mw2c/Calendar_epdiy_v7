set(app_requires epdiy2 fatfs sdmmc esp_driver_spi esp_driver_sdspi)

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__freetype" OR
   EXISTS "${CMAKE_CURRENT_LIST_DIR}/../components/freetype" OR
   EXISTS "${CMAKE_CURRENT_LIST_DIR}/../components/espressif__freetype")
    list(APPEND app_requires freetype)
endif()

idf_component_register(
    SRCS
        "main.c"
        "display_screen.c"
        "display_viewport.c"
        "display_font.c"
        "sd_content.c"
        "text_utils.c"
    INCLUDE_DIRS "."
    REQUIRES ${app_requires}
)
