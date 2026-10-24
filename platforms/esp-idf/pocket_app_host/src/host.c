#include "catalog.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "package_info.h"
#include "pocket/app_host.h"
#include "pocket/guest_idf.h"
#include "pocket/package_source.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "pocket_host";

/* Application operations from the OP table in contracts/spec/spec.ts. The
 * numbers are append-only. */
#define OP_APP_TABLE 39
#define OP_APP_LAUNCH 40
#define OP_APP_SHOT 41

#define CONTACTS_MAX 16
#define OUTPUT_MAX sizeof(((package_identity *)0)->output)
#define MESSAGE_MAX 256
#define HOME_FAILURES_MAX 3

typedef struct {
  char *data;
  size_t length, capacity;
  bool failed;
} text;

typedef struct {
  uint32_t id;
  float start_y;
} edge_contact;

struct pocket_app_host {
  pocket_app_host_config config;
  package_profile profile;
  package_identity home;
  float logical_height;
  catalog apps;

  /* Current session. */
  pocket_runtime *runtime;
  pocket_guest_idf *guest;
  pocket_memory_package package;
  uint8_t *package_storage;
  const catalog_app *app; /* null while home runs */

  /* Requests raised during a tick, applied after it returns. */
  bool switch_requested;
  const catalog_app *next;
  char resume[OUTPUT_MAX];
  bool has_error;
  char error_output[OUTPUT_MAX];
  char error_message[MESSAGE_MAX];

  /* appTable result bytes; valid until the next dispatch. */
  text table;

  /* Input policy: contacts that start in the home edge never reach the
   * guest; after a switch every contact is withheld until all lift. */
  edge_contact edge[CONTACTS_MAX];
  size_t edge_count;
  uint32_t forwarded[CONTACTS_MAX];
  size_t forwarded_count;
  bool suppress;
  pocket_input_frame_builder *outer;
};

/* ---- appTable JSON ---- */

static void put(text *t, const char *s, size_t n) {
  if (t->failed)
    return;
  if (t->length + n + 1 > t->capacity) {
    size_t capacity = t->capacity ? t->capacity : 1024;
    while (capacity < t->length + n + 1)
      capacity *= 2;
    char *grown = realloc(t->data, capacity);
    if (!grown) {
      t->failed = true;
      return;
    }
    t->data = grown;
    t->capacity = capacity;
  }
  memcpy(t->data + t->length, s, n);
  t->length += n;
  t->data[t->length] = 0;
}

static void put_raw(text *t, const char *s) { put(t, s, strlen(s)); }

static void put_string(text *t, const char *s) {
  put(t, "\"", 1);
  for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
    if (*c == '"' || *c == '\\') {
      const char escaped[2] = {'\\', (char)*c};
      put(t, escaped, 2);
    } else if (*c < 0x20) {
      char escaped[8];
      snprintf(escaped, sizeof(escaped), "\\u%04x", *c);
      put(t, escaped, 6);
    } else {
      put(t, (const char *)c, 1);
    }
  }
  put(t, "\"", 1);
}

static bool build_table(pocket_app_host *h) {
  text *t = &h->table;
  t->length = 0;
  t->failed = false;
  put_raw(t, "{\"apps\":[");
  for (size_t i = 0; i < h->apps.count; i++) {
    const package_identity *app = &h->apps.apps[i].identity;
    char bytes[32];
    snprintf(bytes, sizeof(bytes), ",\"bytes\":%llu}",
             (unsigned long long)app->bytes);
    put_raw(t, i ? ",{\"output\":" : "{\"output\":");
    put_string(t, app->output);
    put_raw(t, ",\"id\":");
    put_string(t, app->id);
    put_raw(t, ",\"title\":");
    put_string(t, app->title);
    put_raw(t, bytes);
  }
  put_raw(t, "],\"current\":");
  put_string(t, h->app ? h->app->identity.output : h->home.output);
  put_raw(t, ",\"resume\":");
  if (h->resume[0])
    put_string(t, h->resume);
  else
    put_raw(t, "null");
  put_raw(t, ",\"launcher\":");
  put_string(t, h->home.output);
  put_raw(t, h->apps.storage ? ",\"storage\":\"ready\""
                             : ",\"storage\":\"missing\"");
  put_raw(t, ",\"lastError\":");
  if (h->has_error) {
    put_raw(t, "{\"output\":");
    put_string(t, h->error_output);
    put_raw(t, ",\"message\":");
    put_string(t, h->error_message);
    put_raw(t, "}");
  } else {
    put_raw(t, "null");
  }
  put_raw(t, ",\"installErrors\":[");
  for (size_t i = 0; i < h->apps.error_count; i++) {
    put_raw(t, i ? ",{\"file\":" : "{\"file\":");
    put_string(t, h->apps.errors[i].file);
    put_raw(t, ",\"message\":");
    put_string(t, h->apps.errors[i].message);
    put_raw(t, "}");
  }
  put_raw(t, "]}");
  return !t->failed;
}

/* ---- switching ---- */

static void request(pocket_app_host *h, const catalog_app *next,
                    const char *resume) {
  h->switch_requested = true;
  h->next = next;
  snprintf(h->resume, sizeof(h->resume), "%s", resume ? resume : "");
}

static bool request_launch(pocket_app_host *h, const char *output) {
  const catalog_app *app = catalog_find(&h->apps, output);
  if (!app && strcmp(output, h->home.output))
    return false;
  h->has_error = false;
  request(h, app, NULL);
  return true;
}

static void record_error(pocket_app_host *h, const char *output,
                         const char *message) {
  ESP_LOGE(TAG, "%s: %s", output, message);
  h->has_error = true;
  snprintf(h->error_output, sizeof(h->error_output), "%s", output);
  snprintf(h->error_message, sizeof(h->error_message), "%s", message);
}

/* ---- operation provider ---- */

static int32_t dispatch(void *context, uint16_t op, const pocket_op_arg *args,
                        size_t length, pocket_result *out) {
  pocket_app_host *h = context;
  if (!h || !out || out->struct_size < sizeof(*out))
    return POCKET_STATUS_INVALID;
  const size_t size = out->struct_size;
  *out = (pocket_result){.struct_size = size, .tag = POCKET_RESULT_NONE};
  switch (op) {
  case OP_APP_TABLE:
    if (!build_table(h))
      return POCKET_STATUS_FAILED;
    out->tag = POCKET_RESULT_BYTES;
    out->bytes = (const uint8_t *)h->table.data;
    out->byte_length = h->table.length;
    return POCKET_STATUS_OK;
  case OP_APP_LAUNCH: {
    if (!args || length < sizeof(*args) || args->struct_size < sizeof(*args) ||
        args->tag != POCKET_ARG_BYTES || (args->byte_length && !args->bytes))
      return POCKET_STATUS_INVALID;
    char output[OUTPUT_MAX];
    bool accepted = false;
    if (args->byte_length < sizeof(output)) {
      memcpy(output, args->bytes, args->byte_length);
      output[args->byte_length] = 0;
      accepted = request_launch(h, output);
    }
    out->tag = POCKET_RESULT_I64;
    out->integer = accepted;
    return POCKET_STATUS_OK;
  }
  case OP_APP_SHOT:
    /* No frozen frame is captured on a summon. */
    out->tag = POCKET_RESULT_I64;
    out->integer = -1;
    return POCKET_STATUS_OK;
  }
  return POCKET_STATUS_UNSUPPORTED;
}

static const pocket_op_provider_v1 PROVIDER = {.struct_size = sizeof(PROVIDER),
                                               .dispatch = dispatch};
static const uint16_t OPERATIONS[] = {OP_APP_TABLE, OP_APP_LAUNCH, OP_APP_SHOT};

/* ---- input policy ---- */

static bool contains(const uint32_t *ids, size_t count, uint32_t id) {
  for (size_t i = 0; i < count; i++)
    if (ids[i] == id)
      return true;
  return false;
}

static edge_contact *find_edge(pocket_app_host *h, uint32_t id) {
  for (size_t i = 0; i < h->edge_count; i++)
    if (h->edge[i].id == id)
      return &h->edge[i];
  return NULL;
}

/* Records arrive packed by their own struct_size; read complete prefixes. */
#define CONTACT_PREFIX (offsetof(pocket_contact, y) + sizeof(float))
#define CANCELLED_PREFIX                                                       \
  (offsetof(pocket_cancelled_contact, id) + sizeof(uint32_t))
#define FRAME_PREFIX                                                           \
  (offsetof(pocket_input_frame, cancelled_count) + sizeof(size_t))

static int32_t filter(void *context, const pocket_input_frame *frame) {
  pocket_app_host *h = context;
  if (!frame || frame->struct_size < FRAME_PREFIX ||
      frame->contact_count > CONTACTS_MAX ||
      frame->cancelled_count > CONTACTS_MAX ||
      (frame->contact_count && !frame->contacts) ||
      (frame->cancelled_count && !frame->cancelled))
    return POCKET_STATUS_INVALID;
  pocket_input_frame copy = {0};
  memcpy(&copy, frame,
         frame->struct_size < sizeof(copy) ? frame->struct_size : sizeof(copy));
  copy.struct_size = sizeof(copy);

  const bool gesture = h->app && h->config.home_edge > 0;
  const float edge_top = h->logical_height - h->config.home_edge;
  pocket_contact forwarded[CONTACTS_MAX];
  uint32_t forwarded_ids[CONTACTS_MAX];
  edge_contact edge[CONTACTS_MAX];
  size_t forwarded_count = 0, edge_count = 0;
  bool home = false;
  const uint8_t *cursor = (const uint8_t *)frame->contacts;
  for (size_t i = 0; i < frame->contact_count; i++) {
    const pocket_contact *c = (const pocket_contact *)cursor;
    if (c->struct_size < CONTACT_PREFIX ||
        c->struct_size % _Alignof(pocket_contact))
      return POCKET_STATUS_INVALID;
    cursor += c->struct_size;
    if (h->suppress)
      continue;
    const edge_contact *tracked = find_edge(h, c->id);
    if (tracked) {
      edge[edge_count++] = *tracked;
      home |= tracked->start_y - c->y >= h->config.home_travel;
    } else if (!contains(h->forwarded, h->forwarded_count, c->id) && gesture &&
               c->y >= edge_top) {
      edge[edge_count++] = (edge_contact){.id = c->id, .start_y = c->y};
    } else {
      forwarded[forwarded_count] = (pocket_contact){.struct_size = sizeof(*c)};
      memcpy(&forwarded[forwarded_count], c,
             c->struct_size < sizeof(*c) ? c->struct_size : sizeof(*c));
      forwarded[forwarded_count].struct_size = sizeof(*c);
      forwarded_ids[forwarded_count++] = c->id;
    }
  }
  pocket_cancelled_contact cancelled[CONTACTS_MAX];
  size_t cancelled_count = 0;
  cursor = (const uint8_t *)frame->cancelled;
  for (size_t i = 0; i < frame->cancelled_count; i++) {
    const pocket_cancelled_contact *c =
        (const pocket_cancelled_contact *)cursor;
    if (c->struct_size < CANCELLED_PREFIX ||
        c->struct_size % _Alignof(pocket_cancelled_contact))
      return POCKET_STATUS_INVALID;
    cursor += c->struct_size;
    if (contains(h->forwarded, h->forwarded_count, c->id))
      cancelled[cancelled_count++] = (pocket_cancelled_contact){
          .struct_size = sizeof(*c), .surface = c->surface, .id = c->id};
  }
  copy.contacts = forwarded_count ? forwarded : NULL;
  copy.contact_count = forwarded_count;
  copy.cancelled = cancelled_count ? cancelled : NULL;
  copy.cancelled_count = cancelled_count;
  const int32_t result = h->outer->append(h->outer->context, &copy);
  if (result != POCKET_STATUS_OK)
    return result;
  if (!frame->contact_count)
    h->suppress = false;
  memcpy(h->edge, edge, edge_count * sizeof(*edge));
  h->edge_count = edge_count;
  memcpy(h->forwarded, forwarded_ids, forwarded_count * sizeof(*forwarded_ids));
  h->forwarded_count = forwarded_count;
  if (home && !h->switch_requested) {
    ESP_LOGI(TAG, "home gesture from %s", h->app->identity.output);
    request(h, NULL, h->app->identity.output);
    h->suppress = true;
  }
  return POCKET_STATUS_OK;
}

static uint32_t proxy_classes(void *context) {
  pocket_app_host *h = context;
  return h->config.input->classes
             ? h->config.input->classes(h->config.input_context)
             : POCKET_INPUT_CLASS_CONTACT;
}

static int32_t proxy_poll(void *context, pocket_input_frame_builder *builder) {
  pocket_app_host *h = context;
  if (!builder || builder->struct_size < sizeof(*builder) || !builder->append)
    return POCKET_STATUS_INVALID;
  h->outer = builder;
  pocket_input_frame_builder inner = {
      .struct_size = sizeof(inner), .context = h, .append = filter};
  const int32_t result = h->config.input->poll(h->config.input_context, &inner);
  h->outer = NULL;
  return result;
}

static const pocket_input_source_v1 PROXY = {
    .struct_size = sizeof(PROXY), .classes = proxy_classes, .poll = proxy_poll};

/* ---- sessions ---- */

static void session_end(pocket_app_host *h) {
  /* Runtime destruction drains rendering and display work first. */
  if (h->runtime)
    pocket_runtime_destroy(h->runtime);
  if (h->guest)
    pocket_guest_idf_destroy(h->guest);
  heap_caps_free(h->package_storage);
  h->runtime = NULL;
  h->guest = NULL;
  h->package_storage = NULL;
  h->package = (pocket_memory_package){0};
}

static bool load_package(pocket_app_host *h, const catalog_app *app,
                         char *message) {
  FILE *file = fopen(app->path, "rb");
  struct stat st;
  if (!file || fstat(fileno(file), &st) != 0 || st.st_size <= 0) {
    if (file)
      fclose(file);
    snprintf(message, MESSAGE_MAX, "cannot open %s", app->path);
    return false;
  }
  const size_t size = (size_t)st.st_size;
  h->package_storage =
      heap_caps_aligned_alloc(16, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  const bool read =
      h->package_storage && fread(h->package_storage, 1, size, file) == size;
  fclose(file);
  if (!read) {
    snprintf(message, MESSAGE_MAX, "%s for %u bytes",
             h->package_storage ? "read failed" : "no PSRAM", (unsigned)size);
    return false;
  }
  package_identity identity;
  const char *error =
      package_identity_memory(h->package_storage, size, &h->profile, &identity);
  if (!error && strcmp(identity.output, app->identity.output))
    error = "package changed after the catalog scan";
  if (error) {
    snprintf(message, MESSAGE_MAX, "%s", error);
    return false;
  }
  h->package = (pocket_memory_package){h->package_storage, size};
  return true;
}

static bool session_start(pocket_app_host *h, const catalog_app *app,
                          char *message) {
  h->app = app;
  if (app) {
    if (!load_package(h, app, message))
      goto failed;
  } else {
    h->package = (pocket_memory_package){h->config.home_package,
                                         h->config.home_package_size};
  }
  pocket_runtime_config config = {.struct_size = sizeof(config),
                                  .host_abi = h->config.host_abi,
                                  .tick_hz = h->config.tick_hz,
                                  .heap_limit = h->config.runtime_heap_limit,
                                  .frame_buffer_budget =
                                      h->config.frame_buffer_budget,
                                  .profile = h->config.profile};
  memcpy(config.profile_hash, h->config.profile_hash, 32);
  h->runtime = pocket_runtime_create(&config);
  const pocket_guest_idf_config guest = {
      .heap_limit = h->config.guest_heap_limit,
      .stack_limit = h->config.guest_stack_limit,
      .prefer_psram = true};
  h->guest = h->runtime ? pocket_guest_idf_create(&guest) : NULL;
  if (!h->runtime || !h->guest) {
    snprintf(message, MESSAGE_MAX, "%s creation failed",
             h->runtime ? "QuickJS guest" : "runtime");
    goto failed;
  }
  pocket_renderer_id renderer = POCKET_RENDERER_SOFTWARE;
  if (h->config.renderer)
    renderer = pocket_runtime_add_renderer(h->runtime, h->config.renderer,
                                           h->config.renderer_context);
  int32_t status =
      renderer == POCKET_RENDERER_INVALID ? POCKET_STATUS_FAILED : 0;
  if (!status)
    status = pocket_runtime_set_guest(h->runtime, pocket_guest_idf_vtable(),
                                      h->guest);
  if (!status)
    status = pocket_runtime_set_package_source(
        h->runtime, pocket_memory_package_vtable(), &h->package);
  if (!status)
    status = pocket_runtime_add_input_source(h->runtime, &PROXY, h);
  if (!status)
    status = pocket_runtime_add_display(h->runtime, h->config.display,
                                        h->config.display_context, renderer);
  if (!status)
    status = pocket_runtime_add_op_provider(
        h->runtime, &PROVIDER, h, OPERATIONS,
        sizeof(OPERATIONS) / sizeof(OPERATIONS[0]));
  if (status) {
    snprintf(message, MESSAGE_MAX, "driver registration status %d",
             (int)status);
    goto failed;
  }
  h->suppress = true;
  h->edge_count = 0;
  h->forwarded_count = 0;
  status = pocket_runtime_boot(h->runtime);
  if (status) {
    const char *guest_error = pocket_guest_idf_error(h->guest);
    snprintf(message, MESSAGE_MAX, "boot status %d%s%s", (int)status,
             guest_error && guest_error[0] ? ": " : "",
             guest_error ? guest_error : "");
    goto failed;
  }
  ESP_LOGI(TAG, "running %s; free internal %u, PSRAM %u",
           app ? app->identity.output : h->home.output,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  return true;
failed:
  session_end(h);
  return false;
}

static void wait_until(uint64_t deadline) {
  for (;;) {
    const int64_t remaining =
        (int64_t)(deadline - (uint64_t)esp_timer_get_time());
    if (remaining <= 0)
      return;
    if (remaining > 2000) {
      const TickType_t ticks = pdMS_TO_TICKS((uint32_t)(remaining / 1000));
      vTaskDelay(ticks > 1 ? ticks - 1 : 1);
    } else {
      esp_rom_delay_us((uint32_t)remaining);
    }
  }
}

/* Ticks until a switch request or a failed tick; returns the failure. */
static bool run_session(pocket_app_host *h, char *message) {
  pocket_tick_schedule *schedule = pocket_tick_schedule_create(
      h->config.tick_hz, (uint64_t)esp_timer_get_time(), h->config.max_lag_us);
  if (!schedule) {
    snprintf(message, MESSAGE_MAX, "tick schedule creation failed");
    return false;
  }
  bool ok = true;
  while (!h->switch_requested) {
    uint64_t deadline = 0;
    if (pocket_tick_schedule_deadline(schedule, &deadline)) {
      snprintf(message, MESSAGE_MAX, "tick schedule failed");
      ok = false;
      break;
    }
    wait_until(deadline);
    pocket_tick_timing timing = {.struct_size = sizeof(timing)};
    uint32_t ready = 0;
    if (pocket_tick_schedule_poll(schedule, (uint64_t)esp_timer_get_time(),
                                  &timing, &ready)) {
      snprintf(message, MESSAGE_MAX, "tick schedule failed");
      ok = false;
      break;
    }
    if (!ready)
      continue;
    const int32_t status = pocket_runtime_tick(h->runtime);
    if (status) {
      const char *guest_error = pocket_guest_idf_error(h->guest);
      snprintf(message, MESSAGE_MAX, "tick status %d%s%s", (int)status,
               guest_error && guest_error[0] ? ": " : "",
               guest_error ? guest_error : "");
      ok = false;
      break;
    }
  }
  pocket_tick_schedule_destroy(schedule);
  return ok;
}

static void host_task(void *context) {
  pocket_app_host *h = context;
  if (h->config.apps_dir) {
    if (h->config.inbox_dir)
      catalog_install(&h->apps, h->config.inbox_dir, h->config.apps_dir,
                      &h->profile, h->home.output);
    catalog_scan(&h->apps, h->config.apps_dir, &h->profile, h->home.output);
  }
  ESP_LOGI(TAG, "%u installed app(s), storage %s", (unsigned)h->apps.count,
           h->apps.storage ? "ready" : "missing");
  const catalog_app *target = NULL;
  unsigned home_failures = 0;
  char message[MESSAGE_MAX];
  for (;;) {
    h->switch_requested = false;
    const char *output = target ? target->identity.output : h->home.output;
    bool ok = session_start(h, target, message);
    if (ok) {
      if (!target)
        home_failures = 0;
      ok = run_session(h, message);
      session_end(h);
    }
    if (ok) {
      target = h->next;
      continue;
    }
    record_error(h, output, message);
    if (target) {
      target = NULL;
      h->resume[0] = 0;
      continue;
    }
    if (++home_failures == HOME_FAILURES_MAX) {
      ESP_LOGE(TAG, "home failed %u times; host stopped", home_failures);
      vTaskSuspend(NULL);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void pocket_app_host_config_defaults(pocket_app_host_config *config) {
  if (!config)
    return;
  *config = (pocket_app_host_config){
      .struct_size = sizeof(*config),
      .runtime_heap_limit = 12u * 1024u * 1024u,
      .guest_heap_limit = 8u * 1024u * 1024u,
      .guest_stack_limit = 64u * 1024u,
      .home_edge = 16,
      .home_travel = 48,
      .task_name = "pocket_host",
      .task_stack_bytes = 128u * 1024u,
      .task_priority = 5,
      .task_core = 0,
      .max_lag_us = 250000,
  };
}

esp_err_t pocket_app_host_start(const pocket_app_host_config *config,
                                pocket_app_host **out) {
  if (!config || !out || config->struct_size < sizeof(*config) ||
      !config->profile || !config->profile_hash || !config->display ||
      !config->input || !config->input->poll || !config->home_package ||
      !config->home_package_size || !config->tick_hz ||
      config->task_stack_bytes <= config->guest_stack_limit)
    return ESP_ERR_INVALID_ARG;
  const pocket_profile_facts *facts = config->profile;
  if (facts->struct_size < sizeof(*facts) || !facts->display ||
      facts->display->struct_size < sizeof(*facts->display) ||
      !facts->display->logical_count || !facts->display->logical)
    return ESP_ERR_INVALID_ARG;
  *out = NULL;
  pocket_app_host *h = calloc(1, sizeof(*h));
  if (!h)
    return ESP_ERR_NO_MEM;
  h->config = *config;
  h->profile = (package_profile){config->profile_hash, config->host_abi};
  h->logical_height = (float)facts->display->logical[1];
  const char *error = package_identity_memory(
      config->home_package, config->home_package_size, &h->profile, &h->home);
  if (error) {
    ESP_LOGE(TAG, "home package: %s", error);
    free(h);
    return ESP_ERR_INVALID_ARG;
  }
  if (xTaskCreatePinnedToCore(
          host_task, config->task_name, config->task_stack_bytes, h,
          config->task_priority, NULL, config->task_core) != pdPASS) {
    free(h);
    return ESP_ERR_NO_MEM;
  }
  *out = h;
  return ESP_OK;
}
