#include "esp_check.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_touch_st7123.h"
#include "m5/tab5.h"

static const char *TAG = "m5_tab5_touch";

esp_err_t m5_tab5_touch_new(esp_lcd_touch_handle_t *out) {
  ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "out");
  *out = NULL;
  const m5_tab5_panel_t kind = m5_tab5_panel();
  ESP_RETURN_ON_FALSE(kind != M5_TAB5_PANEL_UNKNOWN, ESP_ERR_NOT_FOUND, TAG,
                      "panel variant");
  esp_lcd_touch_config_t config = {
      .x_max = M5_TAB5_LCD_WIDTH,
      .y_max = M5_TAB5_LCD_HEIGHT,
      .rst_gpio_num = GPIO_NUM_NC, /* reset is expander 0 P5 */
      .int_gpio_num = M5_TAB5_TOUCH_INT,
  };
  esp_lcd_panel_io_handle_t io = NULL;
  esp_err_t result;
  if (kind == M5_TAB5_PANEL_ILI9881C) {
    /* The INT pull-up to 3.3 V selects address 0x14 at reset and then blocks
     * GT911 reports; hold INT low after the address latch. */
    const gpio_config_t interrupt = {
        .pin_bit_mask = BIT64(M5_TAB5_TOUCH_INT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&interrupt), TAG, "INT");
    ESP_RETURN_ON_ERROR(gpio_set_level(M5_TAB5_TOUCH_INT, 0), TAG, "INT low");
    config.int_gpio_num = GPIO_NUM_NC;
    esp_lcd_panel_io_i2c_config_t io_config =
        ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c(m5_tab5_i2c(), &io_config, &io), TAG,
        "GT911 IO");
    result = esp_lcd_touch_new_i2c_gt911(io, &config, out);
  } else {
    const esp_lcd_panel_io_i2c_config_t io_config =
        ESP_LCD_TOUCH_IO_I2C_ST7123_CONFIG();
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c(m5_tab5_i2c(), &io_config, &io), TAG,
        "ST712x IO");
    result = esp_lcd_touch_new_i2c_st7123(io, &config, out);
  }
  if (result != ESP_OK) {
    esp_lcd_panel_io_del(io);
    ESP_LOGE(TAG, "touch driver: %s", esp_err_to_name(result));
  }
  return result;
}
