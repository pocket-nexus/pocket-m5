#pragma once
#include "esp_lcd_types.h"
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x0,
                                    int y0, int x1, int y1, const void *pixels);
