#include "board.h"
#include "esp_check.h"
#include "esp_lcd_ili9881c.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7123.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_touch_st7123.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "m5/tab5.h"

#include "panel_init_ili9881c.h"
#include "panel_init_st7121.h"
#include "panel_init_st7123.h"

static const char *TAG = "m5_tab5_lcd";

#define DSI_LANES 2
#define DSI_PHY_LDO_CHANNEL 3 /* LDO_VO3 feeds VDD_MIPI_DPHY */
#define DSI_PHY_LDO_MV 2500
#define TOUCH_SETTLE_US 500000

static esp_ldo_channel_handle_t phy_power;

static esp_err_t st712x_firmware(uint8_t *out) {
  esp_lcd_panel_io_handle_t io = NULL;
  const esp_lcd_panel_io_i2c_config_t config =
      ESP_LCD_TOUCH_IO_I2C_ST7123_CONFIG();
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(m5_tab5_i2c(), &config, &io),
                      TAG, "ST712x IO");
  esp_err_t result = esp_lcd_panel_io_rx_param(io, 0x0000, out, 1);
  esp_lcd_panel_io_del(io);
  return result;
}

m5_tab5_panel_t m5_tab5_panel(void) {
  static m5_tab5_panel_t cached = M5_TAB5_PANEL_UNKNOWN;
  if (cached != M5_TAB5_PANEL_UNKNOWN)
    return cached;
  if (m5_tab5_init() != ESP_OK)
    return M5_TAB5_PANEL_UNKNOWN;
  /* Touch controllers answer on I2C after their firmware boots. */
  const int64_t wait =
      m5_tab5_reset_released_us() + TOUCH_SETTLE_US - esp_timer_get_time();
  if (wait > 0)
    vTaskDelay(pdMS_TO_TICKS(wait / 1000 + 1));
  i2c_master_bus_handle_t bus = m5_tab5_i2c();
  if (i2c_master_probe(bus, ESP_LCD_TOUCH_IO_I2C_ST7123_ADDRESS, 100) ==
      ESP_OK) {
    uint8_t firmware = 0;
    if (st712x_firmware(&firmware) != ESP_OK)
      ESP_LOGE(TAG, "ST712x firmware version read failed");
    else if (firmware == 1)
      cached = M5_TAB5_PANEL_ST7121;
    else if (firmware == 3)
      cached = M5_TAB5_PANEL_ST7123;
    else
      ESP_LOGE(TAG, "unsupported ST712x firmware %u", firmware);
  } else if (i2c_master_probe(bus, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP,
                              100) == ESP_OK) {
    cached = M5_TAB5_PANEL_ILI9881C;
  } else {
    ESP_LOGE(TAG, "no touch controller at 0x55 or 0x14");
  }
  if (cached != M5_TAB5_PANEL_UNKNOWN)
    ESP_LOGI(TAG, "panel %s", m5_tab5_panel_name(cached));
  return cached;
}

const char *m5_tab5_panel_name(m5_tab5_panel_t kind) {
  switch (kind) {
  case M5_TAB5_PANEL_ILI9881C:
    return "ILI9881C/GT911";
  case M5_TAB5_PANEL_ST7123:
    return "ST7123";
  case M5_TAB5_PANEL_ST7121:
    return "ST7121";
  default:
    return "unknown";
  }
}

/* Timings from espressif/m5stack_tab5_noglib 1.3.2. */
static esp_lcd_video_timing_t timing(m5_tab5_panel_t kind) {
  switch (kind) {
  case M5_TAB5_PANEL_ILI9881C:
    return (esp_lcd_video_timing_t){.h_size = M5_TAB5_LCD_WIDTH,
                                    .v_size = M5_TAB5_LCD_HEIGHT,
                                    .hsync_pulse_width = 40,
                                    .hsync_back_porch = 140,
                                    .hsync_front_porch = 40,
                                    .vsync_pulse_width = 4,
                                    .vsync_back_porch = 20,
                                    .vsync_front_porch = 20};
  case M5_TAB5_PANEL_ST7121:
    return (esp_lcd_video_timing_t){.h_size = M5_TAB5_LCD_WIDTH,
                                    .v_size = M5_TAB5_LCD_HEIGHT,
                                    .hsync_pulse_width = 2,
                                    .hsync_back_porch = 40,
                                    .hsync_front_porch = 40,
                                    .vsync_pulse_width = 20,
                                    .vsync_back_porch = 24,
                                    .vsync_front_porch = 200};
  default:
    return (esp_lcd_video_timing_t){.h_size = M5_TAB5_LCD_WIDTH,
                                    .v_size = M5_TAB5_LCD_HEIGHT,
                                    .hsync_pulse_width = 2,
                                    .hsync_back_porch = 40,
                                    .hsync_front_porch = 40,
                                    .vsync_pulse_width = 2,
                                    .vsync_back_porch = 8,
                                    .vsync_front_porch = 220};
  }
}

static esp_err_t create_panel(m5_tab5_display_t *d, uint32_t fb_count) {
  const esp_lcd_dpi_panel_config_t dpi = {
      .virtual_channel = 0,
      .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
      .dpi_clock_freq_mhz = d->kind == M5_TAB5_PANEL_ILI9881C ? 60 : 70,
      .in_color_format = LCD_COLOR_FMT_RGB565,
      .num_fbs = fb_count,
      .video_timing = timing(d->kind),
  };
  const ili9881c_vendor_config_t ili9881c = {
      .init_cmds = disp_init_data_ili9881c,
      .init_cmds_size =
          sizeof(disp_init_data_ili9881c) / sizeof(disp_init_data_ili9881c[0]),
      .mipi_config = {.dsi_bus = d->bus,
                      .dpi_config = &dpi,
                      .lane_num = DSI_LANES},
  };
  const st7123_vendor_config_t st7123 = {
      .init_cmds = d->kind == M5_TAB5_PANEL_ST7121 ? disp_init_data_st7121
                                                   : disp_init_data_st7123,
      .init_cmds_size =
          d->kind == M5_TAB5_PANEL_ST7121
              ? sizeof(disp_init_data_st7121) / sizeof(disp_init_data_st7121[0])
              : sizeof(disp_init_data_st7123) /
                    sizeof(disp_init_data_st7123[0]),
      .mipi_config = {.dsi_bus = d->bus, .dpi_config = &dpi},
  };
  esp_lcd_panel_dev_config_t device = {
      .reset_gpio_num = GPIO_NUM_NC,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 16,
  };
  if (d->kind == M5_TAB5_PANEL_ILI9881C) {
    device.vendor_config = (void *)&ili9881c;
    return esp_lcd_new_panel_ili9881c(d->io, &device, &d->panel);
  }
  device.vendor_config = (void *)&st7123;
  return esp_lcd_new_panel_st7123(d->io, &device, &d->panel);
}

esp_err_t m5_tab5_display_new(uint32_t fb_count, m5_tab5_display_t *out) {
  ESP_RETURN_ON_FALSE(out && fb_count >= 1 && fb_count <= 3,
                      ESP_ERR_INVALID_ARG, TAG, "arguments");
  *out = (m5_tab5_display_t){0};
  ESP_RETURN_ON_ERROR(m5_tab5_init(), TAG, "board");
  out->kind = m5_tab5_panel();
  ESP_RETURN_ON_FALSE(out->kind != M5_TAB5_PANEL_UNKNOWN, ESP_ERR_NOT_FOUND,
                      TAG, "panel variant");
  if (!phy_power) {
    const esp_ldo_channel_config_t ldo = {.chan_id = DSI_PHY_LDO_CHANNEL,
                                          .voltage_mv = DSI_PHY_LDO_MV};
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo, &phy_power), TAG,
                        "DSI PHY power");
  }
  esp_err_t ret = ESP_OK;
  const esp_lcd_dsi_bus_config_t bus = {
      .bus_id = 0,
      .num_data_lanes = DSI_LANES,
      .lane_bit_rate_mbps = out->kind == M5_TAB5_PANEL_ST7121 ? 965 : 1000,
  };
  ESP_GOTO_ON_ERROR(esp_lcd_new_dsi_bus(&bus, &out->bus), failed, TAG,
                    "DSI bus");
  const esp_lcd_dbi_io_config_t dbi = {
      .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8};
  ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_dbi(out->bus, &dbi, &out->io), failed,
                    TAG, "DBI IO");
  ESP_GOTO_ON_ERROR(create_panel(out, fb_count), failed, TAG, "panel");
  ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(out->panel), failed, TAG, "reset");
  ESP_GOTO_ON_ERROR(esp_lcd_panel_init(out->panel), failed, TAG, "init");
  ret = esp_lcd_panel_disp_on_off(out->panel, true);
  if (ret == ESP_ERR_NOT_SUPPORTED)
    ret = ESP_OK;
  ESP_GOTO_ON_ERROR(ret, failed, TAG, "display on");
  return ESP_OK;
failed:
  m5_tab5_display_del(out);
  return ret;
}

void m5_tab5_display_del(m5_tab5_display_t *d) {
  if (!d)
    return;
  if (d->panel)
    esp_lcd_panel_del(d->panel);
  if (d->io)
    esp_lcd_panel_io_del(d->io);
  if (d->bus)
    esp_lcd_del_dsi_bus(d->bus);
  *d = (m5_tab5_display_t){0};
}
