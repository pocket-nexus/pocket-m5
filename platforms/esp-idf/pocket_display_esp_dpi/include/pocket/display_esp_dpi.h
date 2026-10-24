#pragma once
#include "esp_lcd_types.h"
#include "pocket_drivers.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pocket_display_esp_dpi pocket_display_esp_dpi;

/* MIPI-DSI DPI panel created by esp_lcd_new_panel_dpi with num_fbs == 2 and
 * RGB565 input. Create the panel on the core that runs the runtime owner task:
 * the frame-complete interrupt and the flush path must not run concurrently.
 * The panel stays live until destroy. width/height are the panel's h_size and
 * v_size; flip_timeout_ms bounds one wait for scanout to switch buffers. */
typedef struct {
  size_t struct_size;
  esp_lcd_panel_handle_t panel;
  uint32_t width, height;
  uint32_t flip_timeout_ms;
} pocket_display_esp_dpi_config;

pocket_display_esp_dpi *
pocket_display_esp_dpi_create(const pocket_display_esp_dpi_config *config);
const pocket_display_output_v1 *pocket_display_esp_dpi_vtable(void);
/* Host frame storage the runtime must budget: both frame buffers. */
size_t pocket_display_esp_dpi_budget(const pocket_display_esp_dpi *display);
/* Call after external writes to either frame buffer, while no runtime call is
 * active. The next frame of each buffer repaints it. */
void pocket_display_esp_dpi_invalidate(pocket_display_esp_dpi *display);
/* Waits for a pending flip and unregisters the panel callbacks. Does not
 * delete the panel. */
void pocket_display_esp_dpi_destroy(pocket_display_esp_dpi *display);

#ifdef __cplusplus
}
#endif
