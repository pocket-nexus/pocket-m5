if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(REQUIRES m5_bsp_tab5 pocket_io_esp pocket_worker_esp PRIV_REQUIRES esp_timer)
    return()
endif()
get_filename_component(m5_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(worker_bundle "${CMAKE_CURRENT_BINARY_DIR}/worker-io.worker.js")
execute_process(
    COMMAND bun "${POCKETJS_ROOT}/tools/build-worker.ts" "${m5_root}/apps/worker-io/pocket.json" "${CMAKE_CURRENT_BINARY_DIR}"
    RESULT_VARIABLE worker_build_result
)
if(NOT worker_build_result EQUAL 0)
    message(FATAL_ERROR "Worker bundle build failed")
endif()
file(GLOB worker_sources CONFIGURE_DEPENDS "${m5_root}/apps/worker-io/*.ts" "${m5_root}/apps/worker-io/drivers/*.ts")
file(GLOB worker_sdk_sources CONFIGURE_DEPENDS "${POCKETJS_ROOT}/framework/src/io/*.ts")
list(APPEND worker_sdk_sources "${POCKETJS_ROOT}/framework/src/worker.ts"
    "${POCKETJS_ROOT}/framework/src/io.ts" "${POCKETJS_ROOT}/contracts/spec/worker.ts"
    "${POCKETJS_ROOT}/tools/build-worker.ts")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${worker_sources} ${worker_sdk_sources} "${m5_root}/apps/worker-io/pocket.json")
idf_component_register(
    SRCS "main.c"
    REQUIRES m5_bsp_tab5 pocket_io_esp pocket_worker_esp
    PRIV_REQUIRES esp_timer
    EMBED_TXTFILES "${worker_bundle}"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
