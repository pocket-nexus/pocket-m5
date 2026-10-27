#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pocket/worker_esp.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

struct pocket_worker_esp {
  pocket_worker_esp_config config;
  SemaphoreHandle_t mutex, done, ready;
  TaskHandle_t task;
  pocket_worker *core;
  esp_timer_handle_t timer;
  atomic_bool stopping, failed, active;
  esp_err_t startup;
  size_t guest_used, guest_peak;
  char console[4][1024];
  unsigned console_count, console_dropped;
};

typedef union {
  size_t size;
  max_align_t alignment;
} alloc_header;
static void *internal_malloc(void *opaque, size_t size) {
  pocket_worker_esp *host = opaque;
  if (!size || size > SIZE_MAX - sizeof(alloc_header))
    return NULL;
  alloc_header *block = heap_caps_malloc(sizeof(*block) + size,
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!block) {
    ESP_LOGE("pocket_worker",
             "allocation failed bytes=%u internal_free=%u largest=%u",
             (unsigned)size,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return NULL;
  }
  block->size = size;
  if (host) {
    host->guest_used += size;
    if (host->guest_used > host->guest_peak)
      host->guest_peak = host->guest_used;
  }
  return block + 1;
}
static void *internal_calloc(void *opaque, size_t count, size_t size) {
  if (count && size > SIZE_MAX / count)
    return NULL;
  void *result = internal_malloc(opaque, count * size);
  if (result)
    memset(result, 0, count * size);
  return result;
}
static void internal_free(void *opaque, void *pointer) {
  pocket_worker_esp *host = opaque;
  if (pointer) {
    if (host)
      host->guest_used -= ((alloc_header *)pointer - 1)->size;
    heap_caps_free((alloc_header *)pointer - 1);
  }
}
static size_t internal_size(const void *pointer) {
  return pointer ? ((const alloc_header *)pointer - 1)->size : 0;
}
static void *internal_realloc(void *opaque, void *pointer, size_t size) {
  if (!pointer)
    return internal_malloc(opaque, size);
  if (!size) {
    internal_free(opaque, pointer);
    return NULL;
  }
  if (size > SIZE_MAX - sizeof(alloc_header))
    return NULL;
  pocket_worker_esp *host = opaque;
  alloc_header *block = (alloc_header *)pointer - 1;
  const size_t previous = block->size;
  alloc_header *next = heap_caps_realloc(block, sizeof(*block) + size,
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!next)
    return NULL;
  next->size = size;
  if (host) {
    host->guest_used = host->guest_used - previous + size;
    if (host->guest_used > host->guest_peak)
      host->guest_peak = host->guest_used;
  }
  return next + 1;
}
static const JSMallocFunctions allocator = {.js_malloc = internal_malloc,
                                            .js_calloc = internal_calloc,
                                            .js_free = internal_free,
                                            .js_realloc = internal_realloc,
                                            .js_malloc_usable_size =
                                                internal_size};

static void core_lock(void *context) {
  pocket_worker_esp *worker = context;
  xSemaphoreTake(worker->mutex, portMAX_DELAY);
}
static void core_unlock(void *context) {
  pocket_worker_esp *worker = context;
  xSemaphoreGive(worker->mutex);
}
static void wake(void *context) {
  pocket_worker_esp *worker = context;
  xTaskNotifyGive(worker->task);
}
static uint64_t now(void *context) {
  (void)context;
  return (uint64_t)esp_timer_get_time();
}
static JSValue console_log(JSContext *context, JSValueConst self, int argc,
                           JSValueConst *argv, int magic, JSValue *data) {
  (void)self;
  (void)magic;
  size_t size = 0;
  uint8_t *stored = JS_GetArrayBuffer(context, &size, data[0]);
  if (!stored || size != sizeof(pocket_worker_esp *))
    return JS_ThrowInternalError(context, "console owner unavailable");
  pocket_worker_esp *host;
  memcpy(&host, stored, sizeof(host));
  for (int i = 0; i < argc; ++i) {
    if (host->console_count == 4) {
      ++host->console_dropped;
      continue;
    }
    const char *text = JS_ToCString(context, argv[i]);
    if (!text)
      return JS_EXCEPTION;
    const size_t length = strnlen(text, 1023);
    memcpy(host->console[host->console_count], text, length);
    host->console[host->console_count++][length] = 0;
    JS_FreeCString(context, text);
  }
  return JS_UNDEFINED;
}
static void drain_console(pocket_worker_esp *host) {
  /* Console transmission can block on UART/USB consumers. Drain a bounded
   * batch after the engine measures the JS turn, never inside guest code. */
  for (unsigned i = 0; i < host->console_count; ++i)
    ESP_LOGI("worker", "%s", host->console[i]);
  if (host->console_dropped)
    ESP_LOGW("worker", "console dropped %u records", host->console_dropped);
  host->console_count = host->console_dropped = 0;
}
static int32_t mount(void *opaque, JSContext *context) {
  pocket_worker_esp *host = opaque;
  JSValue global = JS_GetGlobalObject(context);
  JSValue console = JS_NewObject(context);
  if (JS_IsException(console)) {
    JS_FreeValue(context, global);
    return POCKET_WORKER_FAILED;
  }
  JSValue owner =
      JS_NewArrayBufferCopy(context, (const uint8_t *)&host, sizeof(host));
  if (JS_IsException(owner)) {
    JS_FreeValue(context, console);
    JS_FreeValue(context, global);
    return POCKET_WORKER_FAILED;
  }
  const char *names[] = {"log", "error"};
  for (size_t i = 0; i < 2; ++i) {
    JSValue function =
        JS_NewCFunctionData(context, console_log, 1, 0, 1, &owner);
    if (JS_IsException(function) ||
        JS_SetPropertyStr(context, console, names[i], function) < 0) {
      JS_FreeValue(context, owner);
      JS_FreeValue(context, console);
      JS_FreeValue(context, global);
      return POCKET_WORKER_FAILED;
    }
  }
  JS_FreeValue(context, owner);
  const int result = JS_SetPropertyStr(context, global, "console", console);
  JS_FreeValue(context, global);
  return result < 0 ? POCKET_WORKER_FAILED : POCKET_WORKER_OK;
}

static void run(void *opaque) {
  pocket_worker_esp *host = opaque;
  host->task = xTaskGetCurrentTaskHandle();
  const esp_timer_create_args_t timer = {.callback = wake,
                                         .arg = host,
                                         .dispatch_method = ESP_TIMER_TASK,
                                         .name = "worker_deadline"};
  pocket_worker *core = NULL;
  pocket_worker_quickjs *realm = NULL;
  host->startup = esp_timer_create(&timer, &host->timer);
  if (host->startup != ESP_OK)
    goto done;
  const pocket_worker_config config = {.struct_size = sizeof(config),
                                       .generation = host->config.generation,
                                       .nodes = host->config.nodes,
                                       .node_count = host->config.node_count,
                                       .context = host,
                                       .lock = core_lock,
                                       .unlock = core_unlock,
                                       .wake = wake,
                                       .now_micros = now};
  core = pocket_worker_create(&config);
  host->core = core;
  if (!core) {
    host->startup = ESP_ERR_NO_MEM;
    goto done;
  }
  const pocket_worker_quickjs_config qjs = {
      .struct_size = sizeof(qjs),
      .worker = core,
      .heap_limit = host->config.heap_limit,
      .stack_limit = host->config.stack_limit,
      .max_turn_micros = host->config.turn_micros,
      .max_jobs = 8,
      .max_facts = 16,
      .max_bytes = 8192,
      .window_micros = host->config.window_micros,
      .quota_micros = host->config.quota_micros,
      .context = host,
      .now_micros = now,
      .mount = mount,
      .allocator = &allocator,
      .allocator_context = host};
  realm = pocket_worker_quickjs_create(&qjs);
  if (!realm) {
    host->startup = ESP_ERR_NO_MEM;
    goto done;
  }
  ESP_LOGI("pocket_worker",
           "before load internal_free=%u largest=%u heap_limit=%u",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned)host->config.heap_limit);
  if (pocket_worker_quickjs_eval(realm, host->config.source,
                                 host->config.source_size,
                                 "worker.js") != POCKET_WORKER_OK) {
    ESP_LOGE("pocket_worker", "load: %s", pocket_worker_quickjs_error(realm));
    host->startup = ESP_FAIL;
    goto done;
  }
  drain_console(host);
  ESP_LOGI("pocket_worker",
           "loaded guest_used=%u guest_peak=%u internal_free=%u",
           (unsigned)host->guest_used, (unsigned)host->guest_peak,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  xSemaphoreGive(host->ready);
  while (!atomic_load(&host->active) && !atomic_load(&host->stopping))
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  while (!atomic_load(&host->stopping)) {
    const pocket_worker_context turn = {.struct_size = sizeof(turn),
                                        .now_micros = now(host)};
    const int32_t result = pocket_worker_quickjs_step(realm, &turn);
    drain_console(host);
    if (result < 0) {
      ESP_LOGE("pocket_worker", "fault: %s",
               pocket_worker_quickjs_error(realm));
      atomic_store(&host->failed, true);
      break;
    }
    if (result > 0)
      continue;
    /* Notification counts survive the queue-check/wait race. An esp_timer
     * one-shot wakes exact deadlines; there is no global worker tick. */
    const uint64_t deadline = pocket_worker_quickjs_deadline(realm);
    const uint64_t current = now(host);
    esp_timer_stop(host->timer);
    if (deadline != UINT64_MAX && deadline && deadline <= current)
      continue;
    if (deadline && deadline != UINT64_MAX)
      esp_timer_start_once(host->timer, deadline - current);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
done:
  /* A failed realm retains the core until its UI owner detaches all channel
   * mirrors and calls stop. Native queues remain bounded during this interval.
   */
  if (host->startup == ESP_OK && atomic_load(&host->failed)) {
    while (!atomic_load(&host->stopping))
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
  if (host->timer) {
    /* TASK-dispatch callbacks can already be in flight after ordinary stop.
     * Join them before host/task storage can be released by stop(). */
    ESP_ERROR_CHECK(esp_timer_stop_blocking(host->timer, portMAX_DELAY));
  }
  if (core) {
    while (pocket_worker_destroy(core) == POCKET_WORKER_BUSY)
      vTaskDelay(1);
  }
  if (realm) {
    ESP_LOGI("pocket_worker",
             "stopped guest_used=%u guest_peak=%u stack_min_free=%u",
             (unsigned)host->guest_used, (unsigned)host->guest_peak,
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    pocket_worker_quickjs_destroy(realm);
  }
  if (host->timer) {
    esp_timer_delete(host->timer);
    host->timer = NULL;
  }
  if (host->startup != ESP_OK)
    atomic_store(&host->failed, true);
  xSemaphoreGive(host->ready);
  xSemaphoreGive(host->done);
  for (;;)
    vTaskSuspend(NULL);
}

esp_err_t pocket_worker_esp_start(const pocket_worker_esp_config *config,
                                  pocket_worker_esp **out) {
  if (!config || config->struct_size < sizeof(*config) || !out ||
      !config->source || !config->source_size || !config->heap_limit ||
      !config->stack_limit || config->task_stack_size < 8192 ||
      !config->generation || !config->turn_micros || !config->quota_micros ||
      config->quota_micros >= config->window_micros)
    return ESP_ERR_INVALID_ARG;
  *out = NULL;
  pocket_worker_esp *host = calloc(1, sizeof(*host));
  if (!host)
    return ESP_ERR_NO_MEM;
  host->config = *config;
  atomic_init(&host->stopping, false);
  atomic_init(&host->failed, false);
  atomic_init(&host->active, !config->defer_turns);
  host->mutex = xSemaphoreCreateMutex();
  host->ready = xSemaphoreCreateBinary();
  host->done = xSemaphoreCreateBinary();
  if (!host->mutex || !host->ready || !host->done ||
      xTaskCreatePinnedToCore(run, "pocket_worker", config->task_stack_size,
                              host, config->task_priority, &host->task,
                              config->task_core) != pdPASS) {
    if (host->mutex)
      vSemaphoreDelete(host->mutex);
    if (host->ready)
      vSemaphoreDelete(host->ready);
    if (host->done)
      vSemaphoreDelete(host->done);
    free(host);
    return ESP_ERR_NO_MEM;
  }
  xSemaphoreTake(host->ready, portMAX_DELAY);
  if (host->startup != ESP_OK) {
    const esp_err_t result = host->startup;
    pocket_worker_esp_stop(host, UINT32_MAX);
    return result;
  }
  *out = host;
  return ESP_OK;
}
esp_err_t pocket_worker_esp_stop(pocket_worker_esp *host, uint32_t timeout_ms) {
  if (!host)
    return ESP_ERR_INVALID_ARG;
  atomic_store(&host->stopping, true);
  /* Do not notify a task which has already deleted itself. */
  if (xSemaphoreTake(host->done, 0) != pdTRUE) {
    wake(host);
    const TickType_t wait =
        timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(host->done, wait) != pdTRUE)
      return ESP_ERR_TIMEOUT;
  }
  vTaskDelete(host->task);
  vSemaphoreDelete(host->done);
  vSemaphoreDelete(host->ready);
  vSemaphoreDelete(host->mutex);
  free(host);
  return ESP_OK;
}
bool pocket_worker_esp_failed(pocket_worker_esp *host) {
  return host && atomic_load(&host->failed);
}

pocket_worker *pocket_worker_esp_core(pocket_worker_esp *host) {
  return host ? host->core : NULL;
}

void pocket_worker_esp_resume(pocket_worker_esp *host) {
  if (!host)
    return;
  atomic_store(&host->active, true);
  wake(host);
}
