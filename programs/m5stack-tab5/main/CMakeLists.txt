idf_component_register(
    SRCS "main.c" "tab5_camera_media.c" "tab5_keyboard.c"
    REQUIRES
        esp_driver_dma
        esp_driver_i2c
        esp_driver_ppa
        esp_lcd
        esp_mm
        esp_timer
        pocket_display_esp_dpi
        pocket_guest_quickjs
        pocket_render_esp_ppa
        pocket_runner
        pocket_runtime
        pocket_camera
        pocket_http_esp
        pocket_media
        pocket_system
        pocket_worker_esp
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)

# Pocket Home is built in pocket-shell against home.runtime-config.ts.
set(POCKETJS_TAB5_HOME_PACKAGE "" CACHE FILEPATH "Pocket Home's home-main.pocket")
if(NOT POCKETJS_TAB5_HOME_PACKAGE)
    message(FATAL_ERROR "The launcher needs -DPOCKETJS_TAB5_HOME_PACKAGE=<home-main.pocket>")
endif()
pocketjs_embed_package(
    TARGET ${COMPONENT_LIB}
    NAME app
    PACKAGE "${POCKETJS_TAB5_HOME_PACKAGE}"
    RUNTIME_CONFIG "../home.runtime-config.ts")
