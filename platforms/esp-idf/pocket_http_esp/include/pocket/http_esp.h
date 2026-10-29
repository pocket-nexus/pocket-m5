#pragma once
#include "pocket/worker.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pocket_http_esp pocket_http_esp;

typedef struct {
  uint32_t struct_size;
  /* True while the network has an address, or NULL. A request started while
   * it is false fails before it resolves its host. */
  bool (*online)(void *context);
  /* Waits at most `ms` for the wall clock and returns true once it is set,
   * or NULL when TLS does not check certificate dates. An https:// hop waits
   * through it within its request's timeout. */
  bool (*clock_wait)(void *context, uint32_t ms);
  void *context;
  /* User-Agent of a request that names none; NULL selects "PocketJS/1". */
  const char *user_agent;
  /* heap_caps capabilities of the request storage and of the request task
   * stacks; zero selects internal RAM. */
  uint32_t memory_caps, task_stack_caps;
  int task_core;
  unsigned task_priority;
} pocket_http_esp_config;

pocket_http_esp *pocket_http_esp_create(const pocket_http_esp_config *config);
const pocket_worker_driver *pocket_http_esp_driver(pocket_http_esp *driver);
/* Call after the worker core has closed all handles. */
int32_t pocket_http_esp_destroy(pocket_http_esp *driver);

#ifdef __cplusplus
}
#endif
