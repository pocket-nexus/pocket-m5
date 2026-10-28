/* Host cache model for the real DPI driver. CPU and scanout memory are
 * separate so publishing a sparse damage list cannot conceal unflushed rows. */
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/semphr.h"
#include "pocket/display_esp_dpi_flip.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 8, HEIGHT = 6 };
struct mock_panel {
  uint16_t cpu[2][WIDTH * HEIGHT];
  uint16_t memory[2][WIDTH * HEIGHT];
  esp_lcd_dpi_panel_event_callbacks_t callbacks;
  void *context;
  unsigned submissions;
  int x0, y0, x1, y1;
};

void *heap_caps_calloc(size_t count, size_t size, unsigned caps) {
  (void)caps;
  return calloc(count, size);
}
void heap_caps_free(void *pointer) { free(pointer); }
TickType_t xTaskGetTickCount(void) { return 0; }
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *storage) {
  return storage;
}
void vSemaphoreDelete(SemaphoreHandle_t semaphore) { (void)semaphore; }
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore,
                                 BaseType_t *woken) {
  (void)semaphore;
  *woken = pdFALSE;
  return pdTRUE;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout) {
  (void)semaphore;
  (void)timeout;
  assert(!"test must complete a flip before the next acquire");
  return pdFALSE;
}
esp_err_t esp_lcd_dpi_panel_get_frame_buffer(esp_lcd_panel_handle_t panel,
                                             unsigned count, void **first,
                                             void **second) {
  assert(count == 2);
  *first = panel->cpu[0];
  *second = panel->cpu[1];
  return ESP_OK;
}
esp_err_t esp_lcd_dpi_panel_register_event_callbacks(
    esp_lcd_panel_handle_t panel,
    const esp_lcd_dpi_panel_event_callbacks_t *callbacks, void *context) {
  panel->callbacks = *callbacks;
  panel->context = context;
  return ESP_OK;
}
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x0,
                                    int y0, int x1, int y1,
                                    const void *pixels) {
  assert(0 <= x0 && x0 < x1 && x1 <= WIDTH);
  assert(0 <= y0 && y0 < y1 && y1 <= HEIGHT);
  unsigned buffer = 2;
  for (unsigned i = 0; i < 2; ++i)
    if (pixels == &panel->cpu[i][y0 * WIDTH + x0])
      buffer = i;
  assert(buffer < 2);
  /* esp_lcd's existing-framebuffer path writes back whole physical rows. */
  memcpy(&panel->memory[buffer][y0 * WIDTH], &panel->cpu[buffer][y0 * WIDTH],
         (size_t)(y1 - y0) * WIDTH * sizeof(uint16_t));
  panel->submissions++;
  panel->x0 = x0;
  panel->y0 = y0;
  panel->x1 = x1;
  panel->y1 = y1;
  return ESP_OK;
}
static pocket_rect rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
  return (pocket_rect){.struct_size = sizeof(pocket_rect),
                       .x = x,
                       .y = y,
                       .width = w,
                       .height = h};
}
static void exercise_region(uint32_t y, uint32_t height) {
  struct mock_panel panel = {0};
  const pocket_display_esp_dpi_flip_config config = {.struct_size =
                                                         sizeof(config),
                                                     .panel = &panel,
                                                     .width = WIDTH,
                                                     .height = HEIGHT,
                                                     .flip_timeout_ms = 10};
  pocket_display_esp_dpi_flip *display =
      pocket_display_esp_dpi_flip_create(&config);
  assert(display);
  const pocket_display_output_v1 *driver = pocket_display_esp_dpi_flip_vtable();
  pocket_rect region = rect(0, y, WIDTH, height);
  pocket_target target = {.struct_size = sizeof(target)};
  assert(driver->acquire(display, &region, &target) == POCKET_STATUS_OK);
  assert(target.surface->pixels == (uint8_t *)&panel.cpu[1][y * WIDTH]);

  /* A PPA fill updates memory across the bounding dirty rectangle. The CPU
   * redraws a static glyph in the gap, outside either original damage rect. */
  const size_t glyph = (size_t)(y + 1) * WIDTH + 3;
  panel.memory[1][glyph] = 0x1111;
  panel.cpu[1][glyph] = 0xabcd;
  pocket_rect damage[] = {rect(1, y, 2, 1), rect(4, y + height - 1, 2, 1)};
  uint64_t token = 0;
  assert(driver->flush(display, damage, 2, &token) == POCKET_STATUS_OK);
  assert(token);
  assert(panel.memory[1][glyph] == 0xabcd);
  assert(panel.submissions == 1);
  assert(panel.x0 == 0 && panel.x1 == WIDTH);
  assert(panel.y0 == (int)y && panel.y1 == (int)(y + height));
  assert(driver->wait(display, token) == POCKET_STATUS_OK);
  panel.callbacks.on_frame_buf_complete(&panel, NULL, panel.context);

  target.struct_size = sizeof(target);
  assert(driver->acquire(display, &region, &target) == POCKET_STATUS_OK);
  const unsigned submitted = panel.submissions;
  pocket_rect invalid[] = {rect(0, y, WIDTH + 1, 1), rect(WIDTH, y, 1, 1),
                           rect(0, y + height, 1, 1), rect(0, y, 1, 0)};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    /* Validation must finish before publishing even the first valid rect. */
    pocket_rect mixed[] = {damage[0], invalid[i]};
    assert(driver->flush(display, mixed, 2, &token) == POCKET_STATUS_INVALID);
    assert(panel.submissions == submitted);
  }
  assert(driver->flush(display, damage, 0, &token) == POCKET_STATUS_INVALID);
  assert(panel.submissions == submitted);
  pocket_display_esp_dpi_flip_destroy(display);
}
int main(void) {
  exercise_region(0, HEIGHT);
  exercise_region(1, 4);
  puts("DPI acquired-region cache publication: ok");
  return 0;
}
