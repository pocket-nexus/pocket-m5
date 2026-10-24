idf_component_register(
    SRCS "src/display.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd pocket_runtime
    PRIV_REQUIRES esp_hw_support freertos heap
    REQUIRED_IDF_TARGETS esp32p4
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
