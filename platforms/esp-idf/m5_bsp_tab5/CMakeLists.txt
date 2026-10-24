idf_component_register(
    SRCS "src/board.c" "src/display.c" "src/touch.c" "src/sdcard.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_driver_gpio esp_driver_i2c esp_lcd sdmmc
    PRIV_REQUIRES esp_driver_ledc esp_driver_sdmmc esp_hw_support esp_timer fatfs log
    REQUIRED_IDF_TARGETS esp32p4
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
