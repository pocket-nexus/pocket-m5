#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "pocket/display_esp_dpi_flip.h"
#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

struct pocket_display_esp_dpi_flip {
  pocket_display_esp_dpi_flip_config config;
  uint8_t *buffers[2];
  size_t stride, buffer_bytes;
  /* Buffer handed to scanout by the last flush. Scanout switches to it at the
   * first frame-complete interrupt after that flush. */
  uint32_t front;
  bool flip_pending;
  unsigned flip_mark;
  atomic_uint frames;
  StaticSemaphore_t frame_done_storage;
  SemaphoreHandle_t frame_done;
  uint32_t active;
  uint64_t next_token, pending_token, generation;
  pocket_rect region;
  pocket_surface surface;
};

/* The DPI driver restarts DMA with the current frame buffer index before this
 * callback, so each call after a flush proves the previous front is free. */
static IRAM_ATTR bool frame_complete(esp_lcd_panel_handle_t panel,
                                     esp_lcd_dpi_panel_event_data_t *event,
                                     void *context) {
  (void)panel;
  (void)event;
  pocket_display_esp_dpi_flip *d = context;
  atomic_fetch_add_explicit(&d->frames, 1, memory_order_release);
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(d->frame_done, &woken);
  return woken == pdTRUE;
}

static bool wait_flip(pocket_display_esp_dpi_flip *d) {
  if (!d->flip_pending)
    return true;
  const TickType_t timeout = pdMS_TO_TICKS(d->config.flip_timeout_ms);
  const TickType_t started = xTaskGetTickCount();
  while (atomic_load_explicit(&d->frames, memory_order_acquire) ==
         d->flip_mark) {
    const TickType_t elapsed = xTaskGetTickCount() - started;
    if (elapsed >= timeout ||
        xSemaphoreTake(d->frame_done, timeout - elapsed) != pdTRUE)
      if (atomic_load_explicit(&d->frames, memory_order_acquire) ==
          d->flip_mark)
        return false;
  }
  d->flip_pending = false;
  return true;
}

pocket_display_esp_dpi_flip *pocket_display_esp_dpi_flip_create(
    const pocket_display_esp_dpi_flip_config *c) {
  if (!c || c->struct_size < sizeof(*c) || !c->panel || !c->width ||
      !c->height || c->width > INT_MAX || c->height > INT_MAX ||
      !c->flip_timeout_ms || c->width > SIZE_MAX / 2 / c->height / 2)
    return NULL;
  void *fb0 = NULL, *fb1 = NULL;
  if (esp_lcd_dpi_panel_get_frame_buffer(c->panel, 2, &fb0, &fb1) != ESP_OK ||
      !fb0 || !fb1)
    return NULL;
  /* The interrupt reads this state; keep it in internal RAM. */
  pocket_display_esp_dpi_flip *d =
      heap_caps_calloc(1, sizeof(*d), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!d)
    return NULL;
  d->config = *c;
  d->buffers[0] = fb0;
  d->buffers[1] = fb1;
  d->stride = (size_t)c->width * 2;
  d->buffer_bytes = d->stride * c->height;
  d->active = UINT32_MAX;
  d->generation = 1;
  atomic_init(&d->frames, 0);
  d->frame_done = xSemaphoreCreateBinaryStatic(&d->frame_done_storage);
  const esp_lcd_dpi_panel_event_callbacks_t callbacks = {
      .on_frame_buf_complete = frame_complete};
  if (esp_lcd_dpi_panel_register_event_callbacks(c->panel, &callbacks, d) !=
      ESP_OK) {
    vSemaphoreDelete(d->frame_done);
    heap_caps_free(d);
    return NULL;
  }
  return d;
}

size_t
pocket_display_esp_dpi_flip_budget(const pocket_display_esp_dpi_flip *d) {
  return d ? d->buffer_bytes * 2 : 0;
}

void pocket_display_esp_dpi_flip_invalidate(pocket_display_esp_dpi_flip *d) {
  if (d && d->generation < UINT64_MAX)
    d->generation++;
}

void pocket_display_esp_dpi_flip_destroy(pocket_display_esp_dpi_flip *d) {
  if (!d)
    return;
  (void)wait_flip(d);
  const esp_lcd_dpi_panel_event_callbacks_t none = {0};
  esp_lcd_dpi_panel_register_event_callbacks(d->config.panel, &none, NULL);
  vSemaphoreDelete(d->frame_done);
  heap_caps_free(d);
}

static int32_t descriptor(void *context, pocket_display_descriptor *out) {
  pocket_display_esp_dpi_flip *d = context;
  if (!d || !out || out->struct_size < sizeof(*out))
    return POCKET_STATUS_INVALID;
  *out = (pocket_display_descriptor){.struct_size = sizeof(*out),
                                     .role = POCKET_DISPLAY_ROLE_PRIMARY,
                                     .width = d->config.width,
                                     .height = d->config.height,
                                     .pixel_format = POCKET_PIXEL_FORMAT_RGB565,
                                     .stride_bytes = d->stride,
                                     .target_kind = POCKET_TARGET_CPU_VISIBLE,
                                     .flags = POCKET_DISPLAY_DOUBLE_BUFFERED |
                                              POCKET_DISPLAY_PARTIAL_REFRESH,
                                     .refresh_width = 1,
                                     .refresh_height = 1,
                                     .window_x_alignment = 1,
                                     .window_y_alignment = 1,
                                     .vsync_source = POCKET_VSYNC_DEVICE};
  return POCKET_STATUS_OK;
}

/* Returns the buffer that is not in scanout, after a pending flip lands. */
static int32_t acquire(void *context, const pocket_rect *region,
                       pocket_target *out) {
  pocket_display_esp_dpi_flip *d = context;
  if (!d || !region || region->struct_size < sizeof(*region) || !out ||
      out->struct_size < sizeof(*out) || region->x ||
      region->width != d->config.width || !region->height ||
      region->height > d->config.height ||
      region->y > d->config.height - region->height)
    return POCKET_STATUS_INVALID;
  if (!wait_flip(d))
    return POCKET_STATUS_FAILED;
  const uint32_t back = d->front ^ 1;
  d->active = back;
  d->region = *region;
  d->surface = (pocket_surface){
      .struct_size = sizeof(d->surface),
      .pixels = d->buffers[back] + (size_t)region->y * d->stride,
      .byte_length = (size_t)region->height * d->stride,
      .region = &d->region,
      .stride_bytes = d->stride,
      .pixel_format = POCKET_PIXEL_FORMAT_RGB565};
  *out = (pocket_target){.struct_size = sizeof(*out),
                         .kind = POCKET_TARGET_CPU_VISIBLE,
                         .region = &d->region,
                         .pixel_format = POCKET_PIXEL_FORMAT_RGB565,
                         .surface = &d->surface};
  return POCKET_STATUS_OK;
}

/* A renderer may repaint any pixel of the acquired region, including gaps
 * between damage rectangles. Publish every acquired row so CPU writes after
 * PPA fills reach memory before scanout, then select the next frame buffer. */
static int32_t flush(void *context, const pocket_rect *regions, size_t count,
                     uint64_t *token) {
  pocket_display_esp_dpi_flip *d = context;
  if (!d || !regions || !count || !token || d->active > 1 ||
      d->next_token == UINT64_MAX)
    return POCKET_STATUS_INVALID;
  const pocket_rect *r = regions;
  for (size_t i = 0; i < count; i++) {
    if (r->struct_size < sizeof(*r) || r->struct_size % _Alignof(pocket_rect) ||
        !r->width || !r->height || r->width > d->config.width ||
        r->x > d->config.width - r->width || r->y < d->region.y ||
        r->height > d->region.height ||
        r->y - d->region.y > d->region.height - r->height)
      return POCKET_STATUS_INVALID;
    r = (const pocket_rect *)((const uint8_t *)r + r->struct_size);
  }
  uint8_t *base = d->buffers[d->active];
  uint8_t *pixels = base + (size_t)d->region.y * d->stride;
  const esp_err_t result = esp_lcd_panel_draw_bitmap(
      d->config.panel, 0, (int)d->region.y, (int)d->region.width,
      (int)(d->region.y + d->region.height), pixels);
  if (result == ESP_OK) {
    /* Read after the index switch: a later interrupt restarted with it. */
    d->flip_mark = atomic_load_explicit(&d->frames, memory_order_acquire);
    d->front = d->active;
    d->flip_pending = true;
  }
  d->active = UINT32_MAX;
  if (result != ESP_OK) {
    /* The failed publication may leave stale cache lines; repaint both. */
    pocket_display_esp_dpi_flip_invalidate(d);
    return POCKET_STATUS_FAILED;
  }
  *token = d->pending_token = ++d->next_token;
  return POCKET_STATUS_OK;
}

static int32_t wait_display(void *context, uint64_t token) {
  pocket_display_esp_dpi_flip *d = context;
  if (!d || !token || token != d->pending_token)
    return POCKET_STATUS_INVALID;
  /* Cache write-back completed inside flush; scanout reads the flushed
   * buffer until a later flush, and no writer touches it before the next
   * acquire of that buffer waits for the flip away from it. */
  d->pending_token = 0;
  return POCKET_STATUS_OK;
}

static int32_t identity(void *context, pocket_buffer_identity *out) {
  pocket_display_esp_dpi_flip *d = context;
  if (!d || !out || out->struct_size < sizeof(*out))
    return POCKET_STATUS_INVALID;
  *out = (pocket_buffer_identity){.struct_size = sizeof(*out),
                                  .buffer = (d->front ^ 1) + 1,
                                  .generation = d->generation};
  return POCKET_STATUS_OK;
}

static const pocket_display_output_v1 DISPLAY = {.struct_size = sizeof(DISPLAY),
                                                 .descriptor = descriptor,
                                                 .acquire = acquire,
                                                 .flush = flush,
                                                 .wait = wait_display,
                                                 .frame_identity = identity};

const pocket_display_output_v1 *pocket_display_esp_dpi_flip_vtable(void) {
  return &DISPLAY;
}
