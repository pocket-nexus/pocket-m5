# pocket_input_esp_touch

PocketJS input source (`pocket_input_source_v1`, class `CONTACT`) over an
`esp_lcd_touch` handle. Each poll reads up to `CONFIG_ESP_LCD_TOUCH_MAX_POINTS`
points with `esp_lcd_touch_get_data` and reports them as contacts in logical
pixels (`panel coordinate × scale`).

**Contact ids come from position matching, not from the controller.** The
ST7123 driver does not report track ids. Each point takes the id of the
nearest contact from the previous poll within `match_radius` logical pixels;
otherwise it gets a new id. A point that disappears is a lift.

A read failure reports every active contact as cancelled and clears the
contact set. `pocket_input_esp_touch_last_error` returns the last
`esp_err_t`. The program owns controller creation, rotation flags and
destruction; poll and destroy run on the runtime owner task.

This component differs from upstream `pocket_input_touch`, which reports one
contact through the deprecated `esp_lcd_touch_get_coordinates`.
