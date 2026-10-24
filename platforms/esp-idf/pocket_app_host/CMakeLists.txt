idf_component_register(
    SRCS "src/host.c" "src/catalog.c" "src/package_info.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES pocket_runtime
    PRIV_REQUIRES pocket_guest_quickjs esp_timer freertos heap log vfs
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
