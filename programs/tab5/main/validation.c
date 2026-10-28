#include "validation.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_private/esp_cache_private.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* All fields are 32-bit words. The PC writes arguments before request, and
 * waits for done == request before reading a result or issuing another one.
 * This internal-RAM mailbox is accessible through USB JTAG while both cores
 * run. Firmware never interprets a debugger-supplied pointer or byte count. */
typedef struct {
  uint32_t magic, version, words;
  _Atomic uint32_t capture_request, capture_done;
  int32_t capture_status;
  uint32_t pixels, width, height, stride;
  _Atomic uint32_t tap_request, tap_done;
  int32_t tap_status;
  uint32_t tap_x, tap_y, tap_frames;
  uint32_t logical_width, logical_height;
  _Atomic uint32_t polls, flushes;
} validation_mailbox;

_Static_assert(sizeof(validation_mailbox) == 20 * sizeof(uint32_t),
               "debugger mailbox has a fixed word layout");
_Static_assert(sizeof(uintptr_t) == sizeof(uint32_t),
               "debugger mailbox targets ESP32-P4");

validation_mailbox tab5_validation_mailbox;

typedef struct {
  const pocket_display_output_v1 *display;
  void *display_context;
  const pocket_input_source_v1 *input;
  void *input_context;
  uint8_t *captured, *drawing;
  size_t bytes, cache_alignment;
  uint32_t tap_sequence, remaining_frames;
  float tap_x, tap_y;
} validation_state;

static validation_state state;

static int32_t descriptor(void *context, pocket_display_descriptor *out) {
  validation_state *v = context;
  return v->display->descriptor(v->display_context, out);
}

static int32_t acquire(void *context, const pocket_rect *region,
                       pocket_target *out) {
  validation_state *v = context;
  const int32_t result = v->display->acquire(v->display_context, region, out);
  if (result == POCKET_STATUS_OK) {
    v->drawing = NULL;
    if (out->kind == POCKET_TARGET_CPU_VISIBLE && out->surface &&
        out->surface->pixel_format == POCKET_PIXEL_FORMAT_RGB565 &&
        out->surface->stride_bytes == tab5_validation_mailbox.stride &&
        region->x == 0)
      v->drawing = (uint8_t *)out->surface->pixels -
                   (size_t)region->y * tab5_validation_mailbox.stride;
  }
  return result;
}

static int32_t flush(void *context, const pocket_rect *regions, size_t count,
                     uint64_t *token) {
  validation_state *v = context;
  const int32_t result =
      v->display->flush(v->display_context, regions, count, token);
  atomic_fetch_add_explicit(&tab5_validation_mailbox.flushes, 1,
                            memory_order_relaxed);
  const uint32_t request = atomic_load_explicit(
      &tab5_validation_mailbox.capture_request, memory_order_acquire);
  if (request != atomic_load_explicit(&tab5_validation_mailbox.capture_done,
                                      memory_order_relaxed)) {
    int32_t captured = POCKET_STATUS_FAILED;
    /* Render work has finished; DPI flush wrote back CPU-rendered rows and
     * selected the next scanout buffer. Scanout may still be pending, but
     * the owner cannot acquire/rewrite this target until this call returns.
     * Invalidate the source cache before reading PPA/DMA output. The copied
     * buffer stays unchanged until another capture request. */
    if (result == POCKET_STATUS_OK && v->drawing &&
        (uintptr_t)v->drawing % v->cache_alignment == 0 &&
        esp_cache_msync(v->drawing, v->bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C) ==
            ESP_OK) {
      memcpy(v->captured, v->drawing, v->bytes);
      if (esp_cache_msync(v->captured, v->bytes,
                          ESP_CACHE_MSYNC_FLAG_DIR_C2M) == ESP_OK)
        captured = POCKET_STATUS_OK;
    }
    tab5_validation_mailbox.capture_status = captured;
    atomic_store_explicit(&tab5_validation_mailbox.capture_done, request,
                          memory_order_release);
  }
  return result;
}

static int32_t wait_display(void *context, uint64_t token) {
  validation_state *v = context;
  return v->display->wait(v->display_context, token);
}

static int32_t identity(void *context, pocket_buffer_identity *out) {
  validation_state *v = context;
  return v->display->frame_identity(v->display_context, out);
}

static uint32_t classes(void *context) {
  validation_state *v = context;
  return v->input->classes(v->input_context);
}

static int32_t poll(void *context, pocket_input_frame_builder *builder) {
  validation_state *v = context;
  atomic_fetch_add_explicit(&tab5_validation_mailbox.polls, 1,
                            memory_order_relaxed);
  const uint32_t request = atomic_load_explicit(
      &tab5_validation_mailbox.tap_request, memory_order_acquire);
  if (!v->remaining_frames &&
      request != atomic_load_explicit(&tab5_validation_mailbox.tap_done,
                                      memory_order_relaxed)) {
    const uint32_t x = tab5_validation_mailbox.tap_x;
    const uint32_t y = tab5_validation_mailbox.tap_y;
    const uint32_t frames = tab5_validation_mailbox.tap_frames;
    if (x >= tab5_validation_mailbox.logical_width ||
        y >= tab5_validation_mailbox.logical_height || !frames || frames > 30) {
      tab5_validation_mailbox.tap_status = POCKET_STATUS_INVALID;
      atomic_store_explicit(&tab5_validation_mailbox.tap_done, request,
                            memory_order_release);
    } else {
      v->tap_sequence = request;
      v->remaining_frames = frames + 1;
      v->tap_x = (float)x;
      v->tap_y = (float)y;
    }
  }
  if (!v->remaining_frames)
    return v->input->poll(v->input_context, builder);

  /* Replace physical samples for the bounded tap, then append one empty
   * frame to produce the release through the runtime's normal input path. */
  const pocket_contact contact = {.struct_size = sizeof(contact),
                                  .surface = 0,
                                  .id = UINT32_MAX,
                                  .x = v->tap_x,
                                  .y = v->tap_y};
  const bool down = v->remaining_frames > 1;
  const pocket_input_frame frame = {.struct_size = sizeof(frame),
                                    .contacts = down ? &contact : NULL,
                                    .contact_count = down ? 1 : 0};
  const int32_t result = builder->append(builder->context, &frame);
  if (result != POCKET_STATUS_OK || --v->remaining_frames == 0) {
    v->remaining_frames = 0;
    tab5_validation_mailbox.tap_status = result;
    atomic_store_explicit(&tab5_validation_mailbox.tap_done, v->tap_sequence,
                          memory_order_release);
  }
  return result;
}

static const pocket_display_output_v1 DISPLAY = {.struct_size = sizeof(DISPLAY),
                                                 .descriptor = descriptor,
                                                 .acquire = acquire,
                                                 .flush = flush,
                                                 .wait = wait_display,
                                                 .frame_identity = identity};
static const pocket_input_source_v1 INPUT = {
    .struct_size = sizeof(INPUT), .classes = classes, .poll = poll};

esp_err_t tab5_validation_install(pocket_app_host_config *host) {
  if (!host || state.captured || !host->display || !host->input ||
      !host->display->descriptor || !host->display->acquire ||
      !host->display->flush || !host->display->wait ||
      !host->display->frame_identity || host->display->vsync ||
      host->display->move_pixels || host->display->host_damage ||
      !host->profile || !host->profile->display)
    return ESP_ERR_INVALID_ARG;
  pocket_display_descriptor d = {.struct_size = sizeof(d)};
  if (host->display->descriptor(host->display_context, &d) !=
          POCKET_STATUS_OK ||
      d.pixel_format != POCKET_PIXEL_FORMAT_RGB565 || d.width != 720 ||
      d.height != 1280 || d.stride_bytes != 1440)
    return ESP_ERR_NOT_SUPPORTED;
  const uint32_t density = host->profile->display->raster_density;
  if (!density || d.width % density || d.height % density)
    return ESP_ERR_INVALID_ARG;
  state.bytes = (size_t)d.stride_bytes * d.height;
  if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &state.cache_alignment) !=
          ESP_OK ||
      !state.cache_alignment || state.bytes % state.cache_alignment)
    return ESP_ERR_NOT_SUPPORTED;
  state.captured = heap_caps_aligned_alloc(state.cache_alignment, state.bytes,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!state.captured)
    return ESP_ERR_NO_MEM;
  state.display = host->display;
  state.display_context = host->display_context;
  state.input = host->input;
  state.input_context = host->input_context;
  tab5_validation_mailbox =
      (validation_mailbox){.magic = 0x54414235,
                           .version = 1,
                           .words = 20,
                           .pixels = (uint32_t)(uintptr_t)state.captured,
                           .width = d.width,
                           .height = d.height,
                           .stride = d.stride_bytes,
                           .logical_width = d.width / density,
                           .logical_height = d.height / density};
  host->display = &DISPLAY;
  host->display_context = &state;
  host->input = &INPUT;
  host->input_context = &state;
  ESP_LOGW("tab5",
           "USB JTAG UI validation enabled; capture uses %u PSRAM bytes",
           (unsigned)state.bytes);
  return ESP_OK;
}
