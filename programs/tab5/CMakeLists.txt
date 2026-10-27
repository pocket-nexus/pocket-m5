cmake_minimum_required(VERSION 3.22)

get_filename_component(m5_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(POCKETJS_ROOT "${m5_root}/third_party/pocketjs" CACHE PATH "PocketJS source checkout")
option(POCKETJS_ENABLE_WORKER "Compile optional worker host and IO drivers" OFF)
set(ENV{POCKETJS_ENABLE_WORKER} "${POCKETJS_ENABLE_WORKER}")
set(ENV{POCKETJS_ROOT} "${POCKETJS_ROOT}")
set(pocketjs_components "${POCKETJS_ROOT}/platforms/esp-idf")
list(APPEND EXTRA_COMPONENT_DIRS
    "${pocketjs_components}/pocket_runtime"
    "${pocketjs_components}/pocket_guest_quickjs"
    "${pocketjs_components}/pocket_render_esp_ppa"
    "${m5_root}/platforms/esp-idf")

# The PocketJS Rust archive builds from the submodule unless a prepared
# archive is passed with -DPOCKETJS_RUST_FROM_SOURCE=OFF
# -DPOCKET_RUNTIME_ARCHIVE=<path>.
if(NOT DEFINED POCKETJS_RUST_FROM_SOURCE)
    set(POCKETJS_RUST_FROM_SOURCE ON CACHE BOOL "")
endif()
# Build commands run after CMake exits, so preserve the chosen checkout in a
# generated launcher rather than relying on CMake's process environment.
file(WRITE "${CMAKE_BINARY_DIR}/pocket-cli"
    "#!/bin/sh\nexec bun \"${POCKETJS_ROOT}/tools/pocket.ts\" \"$@\"\n")
file(CHMOD "${CMAKE_BINARY_DIR}/pocket-cli"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
set(POCKETJS_CLI "${CMAKE_BINARY_DIR}/pocket-cli" CACHE FILEPATH "PocketJS CLI")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(pocket_m5_tab5)
