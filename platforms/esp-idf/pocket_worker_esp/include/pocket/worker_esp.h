#pragma once
#include "esp_err.h"
#include "pocket/worker_quickjs.h"
#include <stdbool.h>

typedef struct pocket_worker_esp pocket_worker_esp;
typedef struct {
  uint32_t struct_size, generation;
  const pocket_worker_node *nodes;
  size_t node_count;
  const char *source;
  size_t source_size;
  uint32_t heap_limit, stack_limit, task_stack_size;
  /* Optional CPU share: at most quota_micros of JS per window_micros. Zero
   * quota disables it. Set it only when the worker task shares a core with
   * the UI task at a higher priority; FreeRTOS gives a lower-priority UI no
   * time against a busy worker. There is no execution time limit. */
  uint32_t window_micros, quota_micros;
  int task_core;
  unsigned task_priority;
  /* Bootstrap channels/resources, then wait for resume before executing turns.
   */
  bool defer_turns;
} pocket_worker_esp_config;

/* Source, node table and driver objects must outlive the runner. */
esp_err_t pocket_worker_esp_start(const pocket_worker_esp_config *config,
                                  pocket_worker_esp **out);
/* Stops JS dispatch, interrupting a turn in progress, joins all native users,
 * then destroys the realm. A timeout preserves the runner and its buffers;
 * call stop again to join. */
esp_err_t pocket_worker_esp_stop(pocket_worker_esp *worker,
                                 uint32_t timeout_ms);
/* Release a bootstrap-only runner after its UI owner finishes booting. */
void pocket_worker_esp_resume(pocket_worker_esp *worker);
bool pocket_worker_esp_failed(pocket_worker_esp *worker);

/* Channel bridges may borrow this core until stop. Detach every bridge before
 * stop, including when failed() is true. The core stays alive after a fault. */
pocket_worker *pocket_worker_esp_core(pocket_worker_esp *worker);
