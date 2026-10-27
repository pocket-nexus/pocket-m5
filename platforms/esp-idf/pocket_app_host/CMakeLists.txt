set(worker_requires)
if(CONFIG_POCKET_APP_HOST_WORKER OR "$ENV{POCKETJS_ENABLE_WORKER}" STREQUAL "ON")
    list(APPEND worker_requires pocket_worker_esp)
endif()
idf_component_register(
    SRCS "src/host.c" "src/catalog.c" "src/package_info.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES pocket_runtime ${worker_requires}
    PRIV_REQUIRES pocket_guest_quickjs esp_timer freertos heap log vfs
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
