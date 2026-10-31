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
        pocket_audio
        pocket_camera
        pocket_device
        pocket_http_esp
        pocket_io_esp
        pocket_media
        pocket_system
        pocket_worker_esp
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)

# The launcher is Pocket Home, compiled from the pocket-shell submodule
# against home.runtime-config.ts. A package compiled against the same
# profile replaces it.
set(POCKETJS_TAB5_HOME_PACKAGE "" CACHE FILEPATH "Launcher package to embed instead of the submodule's Pocket Home")
if(POCKETJS_TAB5_HOME_PACKAGE)
    pocketjs_embed_package(
        TARGET ${COMPONENT_LIB}
        NAME app
        PACKAGE "${POCKETJS_TAB5_HOME_PACKAGE}"
        RUNTIME_CONFIG "../home.runtime-config.ts")
else()
    set(pocket_shell_3ds "../../../third_party/pocket-shell/shells/3ds")
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${pocket_shell_3ds}/home/pocket.json")
        message(FATAL_ERROR "third_party/pocket-shell is not checked out: git submodule update --init third_party/pocket-shell")
    endif()
    pocketjs_compile_app(
        TARGET ${COMPONENT_LIB}
        NAME app
        MANIFEST "${pocket_shell_3ds}/home/pocket.json"
        RUNTIME_CONFIG "../home.runtime-config.ts"
        PROJECT_ROOT "${pocket_shell_3ds}")
endif()
