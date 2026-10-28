set(tab5_requires m5_bsp_tab5 pocket_app_host pocket_display_esp_dpi_flip
    pocket_input_esp_touch pocket_runtime pocket_render_esp_ppa esp_mm)

if(CONFIG_POCKET_APP_HOST_WORKER OR "$ENV{POCKETJS_ENABLE_WORKER}" STREQUAL "ON")
    list(APPEND tab5_requires pocket_io_esp pocket_worker_esp)
endif()

set(tab5_sources "main.c")
if(CONFIG_TAB5_UI_VALIDATION)
    list(APPEND tab5_sources "validation.c")
endif()

idf_component_register(
    SRCS ${tab5_sources}
    REQUIRES ${tab5_requires}
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)

# The home launcher is compiled for this program's profile and embedded in
# the firmware. Installed apps are compiled the same way by tools/app.ts.
get_filename_component(m5_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(tab5_runtime_config "${CMAKE_CURRENT_LIST_DIR}/../pocket.runtime-config.ts")
if(CONFIG_POCKET_APP_HOST_WORKER)
    set(tab5_runtime_config "${CMAKE_CURRENT_LIST_DIR}/../worker.runtime-config.ts")
endif()
set(TAB5_HOME_MANIFEST "${m5_root}/apps/home/pocket.json" CACHE FILEPATH "Embedded launcher app manifest")
get_filename_component(tab5_home_root "${TAB5_HOME_MANIFEST}" DIRECTORY)
pocketjs_compile_app(
    TARGET ${COMPONENT_LIB}
    NAME home
    MANIFEST "${TAB5_HOME_MANIFEST}"
    PROJECT_ROOT "${tab5_home_root}"
    RUNTIME_CONFIG "${tab5_runtime_config}")
