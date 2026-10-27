#pragma once
#include "esp_err.h"
#include "pocket_runtime.h"
#include "sdkconfig.h"
#ifdef CONFIG_POCKET_APP_HOST_WORKER
#include "pocket/worker_esp.h"
#endif
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pocket_app_host pocket_app_host;

/* One host task runs one guest at a time. Home is the embedded launcher
 * package; installed packages are .pocket files compiled for the same
 * profile. A switch finishes the current tick, destroys the runtime, guest and
 * package storage, then boots the next package on a fresh runtime. */
typedef struct {
  size_t struct_size;
  /* Program profile from `pocket profile export` (pocket_profile.h):
   * pocket_profile.facts, .profile_hash, .host_abi and .tick_hz. The facts
   * stay live for the host lifetime. The home gesture measures the bottom
   * edge of the first logical viewport. */
  const pocket_profile_facts *profile;
  const uint8_t *profile_hash; /* 32 bytes */
  uint32_t host_abi, tick_hz;
  /* Byte limits for each session. */
  size_t runtime_heap_limit, guest_heap_limit, guest_stack_limit;
  size_t frame_buffer_budget;
  /* Program-owned drivers; they outlive the host and every session. A null
   * renderer selects the runtime's software renderer. */
  const pocket_display_output_v1 *display;
  void *display_context;
  const pocket_renderer_v1 *renderer;
  void *renderer_context;
  const pocket_input_source_v1 *input;
  void *input_context;
  /* Embedded home package; storage stays unchanged for the host lifetime. */
  const uint8_t *home_package;
  size_t home_package_size;
  /* Installed packages and the install inbox. Null apps_dir disables the
   * catalog, for example when no SD card is mounted. */
  const char *apps_dir;
  const char *inbox_dir;
  /* A contact that starts within home_edge logical pixels of the bottom edge
   * and travels home_travel pixels upward returns to home. The guest never
   * receives it. Zero home_edge disables the gesture. */
  float home_edge, home_travel;
  /* Owner task. The task stack must exceed guest_stack_limit plus runtime
   * and driver frames. */
  const char *task_name;
  uint32_t task_stack_bytes;
  /* Zero uses IDF's default internal stack. Nonzero selects the capabilities
   * passed to xTaskCreatePinnedToCoreWithCaps (for example PSRAM). A PSRAM
   * owner must not run cache-disabled flash/NVS or deep-sleep operations.
   * A future host destroy API must join and use vTaskDeleteWithCaps. */
  uint32_t task_stack_caps;
  int task_priority, task_core;
  uint64_t max_lag_us;
#ifdef CONFIG_POCKET_APP_HOST_WORKER
  /* Source and generation are supplied from the admitted app session. Node
   * declarations and driver objects are firmware-owned for the host lifetime.
   */
  const pocket_worker_esp_config *worker;
#endif
} pocket_app_host_config;

void pocket_app_host_config_defaults(pocket_app_host_config *config);
/* Copies config, creates the owner task and returns. The task installs inbox
 * packages, scans the catalog and boots home. */
esp_err_t pocket_app_host_start(const pocket_app_host_config *config,
                                pocket_app_host **out);

#ifdef __cplusplus
}
#endif
