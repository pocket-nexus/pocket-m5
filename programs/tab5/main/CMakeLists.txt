set(tab5_requires m5_bsp_tab5 pocket_app_host pocket_display_esp_dpi_flip
    pocket_input_esp_touch pocket_runtime pocket_render_esp_ppa)

idf_component_register(
    SRCS "main.c"
    REQUIRES ${tab5_requires}
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)

# The home launcher is compiled for this program's profile and embedded in
# the firmware. Installed apps are compiled the same way by tools/app.ts.
get_filename_component(m5_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
pocketjs_compile_app(
    TARGET ${COMPONENT_LIB}
    NAME home
    MANIFEST "${m5_root}/apps/home/pocket.json"
    PROJECT_ROOT "${m5_root}/apps/home"
    RUNTIME_CONFIG "${CMAKE_CURRENT_LIST_DIR}/../pocket.runtime-config.ts")
