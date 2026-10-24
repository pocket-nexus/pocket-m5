#pragma once
#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "pocket_drivers.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pocket_input_esp_touch pocket_input_esp_touch;

/* Borrows an initialised esp_lcd_touch handle; the program owns controller
 * setup, rotation flags and destruction. Coordinates are multiplied by
 * scale_x/scale_y into logical pixels. match_radius is the largest logical
 * distance a contact may move between two polls and keep its id. */
typedef struct {
  size_t struct_size;
  esp_lcd_touch_handle_t touch;
  uint32_t surface;
  float scale_x, scale_y;
  float match_radius;
} pocket_input_esp_touch_config;

/* Up to CONFIG_ESP_LCD_TOUCH_MAX_POINTS contacts per poll. Contact ids come
 * from nearest-position matching against the previous poll, so controllers
 * without hardware track ids keep stable ids. A read failure cancels every
 * active contact. Poll and destroy run on the runtime owner task. */
pocket_input_esp_touch *
pocket_input_esp_touch_create(const pocket_input_esp_touch_config *config);
const pocket_input_source_v1 *pocket_input_esp_touch_vtable(void);
esp_err_t
pocket_input_esp_touch_last_error(const pocket_input_esp_touch *touch);
void pocket_input_esp_touch_destroy(pocket_input_esp_touch *touch);

#ifdef __cplusplus
}
#endif
