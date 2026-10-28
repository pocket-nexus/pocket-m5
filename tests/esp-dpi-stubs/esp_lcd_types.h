#pragma once
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
typedef struct mock_panel *esp_lcd_panel_handle_t;
typedef struct {
  unsigned unused;
} esp_lcd_dpi_panel_event_data_t;
typedef struct {
  bool (*on_frame_buf_complete)(esp_lcd_panel_handle_t panel,
                                esp_lcd_dpi_panel_event_data_t *event,
                                void *context);
} esp_lcd_dpi_panel_event_callbacks_t;
