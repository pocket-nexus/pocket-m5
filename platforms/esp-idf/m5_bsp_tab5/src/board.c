#include "board.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_io_expander_pi4ioe5v6408.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "m5/tab5.h"
#include "sdkconfig.h"

static const char *TAG = "m5_tab5";

/* Expander 0 (0x43). */
#define EXP0_SPEAKER IO_EXPANDER_PIN_NUM_1
#define EXP0_EXT_5V IO_EXPANDER_PIN_NUM_2
#define EXP0_LCD_RESET IO_EXPANDER_PIN_NUM_4
#define EXP0_TOUCH_RESET IO_EXPANDER_PIN_NUM_5
#define EXP0_CAMERA IO_EXPANDER_PIN_NUM_6
#define EXP0_HEADPHONE IO_EXPANDER_PIN_NUM_7
/* Expander 1 (0x44). */
#define EXP1_WIFI IO_EXPANDER_PIN_NUM_0
#define EXP1_USB_5V IO_EXPANDER_PIN_NUM_3
#define EXP1_POWER_OFF IO_EXPANDER_PIN_NUM_4
#define EXP1_QUICK_CHARGE_N IO_EXPANDER_PIN_NUM_5
#define EXP1_USB_C_VBUS IO_EXPANDER_PIN_NUM_6
#define EXP1_CHARGE IO_EXPANDER_PIN_NUM_7

#ifdef CONFIG_M5_TAB5_CHARGE_ENABLE
#define CHARGE_ON true
#else
#define CHARGE_ON false
#endif
#ifdef CONFIG_M5_TAB5_QUICK_CHARGE_ENABLE
#define QUICK_CHARGE_ON true
#else
#define QUICK_CHARGE_ON false
#endif

#define BACKLIGHT_RESOLUTION LEDC_TIMER_10_BIT
#define BACKLIGHT_MAX_DUTY 1023u

static i2c_master_bus_handle_t bus;
static esp_io_expander_handle_t expanders[2];
static int64_t reset_released_us;
static bool initialized;

int64_t m5_tab5_reset_released_us(void) { return reset_released_us; }

i2c_master_bus_handle_t m5_tab5_i2c(void) { return bus; }

esp_io_expander_handle_t m5_tab5_expander(m5_tab5_expander_t expander) {
  return (unsigned)expander < 2 ? expanders[expander] : NULL;
}

/* PI4IOE5V6408 rejects level writes to inputs; select direction first. */
static esp_err_t drive(esp_io_expander_handle_t expander, uint32_t pins,
                       bool level) {
  ESP_RETURN_ON_ERROR(esp_io_expander_set_output_mode(
                          expander, pins, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL),
                      TAG, "output mode");
  ESP_RETURN_ON_ERROR(
      esp_io_expander_set_dir(expander, pins, IO_EXPANDER_OUTPUT), TAG,
      "direction");
  return esp_io_expander_set_level(expander, pins, level);
}

/* The panel reset input has no 1.8 V pull-up; release it as an input with the
 * expander pull-up instead of driving 3.3 V. */
static esp_err_t release_lcd_reset(esp_io_expander_handle_t expander) {
  ESP_RETURN_ON_ERROR(esp_io_expander_set_pullupdown(expander, EXP0_LCD_RESET,
                                                     IO_EXPANDER_PULL_UP),
                      TAG, "LCD reset pull-up");
  ESP_RETURN_ON_ERROR(
      esp_io_expander_set_dir(expander, EXP0_LCD_RESET, IO_EXPANDER_INPUT), TAG,
      "LCD reset direction");
  return esp_io_expander_set_output_mode(expander, EXP0_LCD_RESET,
                                         IO_EXPANDER_OUTPUT_MODE_OPEN_DRAIN);
}

static esp_err_t init_expander0(esp_io_expander_handle_t e) {
  ESP_RETURN_ON_ERROR(drive(e, EXP0_SPEAKER | EXP0_EXT_5V | EXP0_CAMERA, false),
                      TAG, "rails");
  ESP_RETURN_ON_ERROR(drive(e, EXP0_LCD_RESET | EXP0_TOUCH_RESET, false), TAG,
                      "reset assert");
  vTaskDelay(pdMS_TO_TICKS(20));
  /* GT911 latches I2C address 0x14 from its pulled-up INT line here. */
  ESP_RETURN_ON_ERROR(esp_io_expander_set_level(e, EXP0_TOUCH_RESET, 1), TAG,
                      "touch reset release");
  ESP_RETURN_ON_ERROR(release_lcd_reset(e), TAG, "LCD reset release");
  reset_released_us = esp_timer_get_time();
  return esp_io_expander_set_dir(e, EXP0_HEADPHONE, IO_EXPANDER_INPUT);
}

static esp_err_t init_expander1(esp_io_expander_handle_t e) {
  ESP_RETURN_ON_ERROR(drive(e, EXP1_WIFI | EXP1_USB_5V | EXP1_POWER_OFF, false),
                      TAG, "rails");
  ESP_RETURN_ON_ERROR(drive(e, EXP1_QUICK_CHARGE_N, !QUICK_CHARGE_ON), TAG,
                      "quick charge");
  ESP_RETURN_ON_ERROR(drive(e, EXP1_CHARGE, CHARGE_ON), TAG, "charge");
  /* VBUS detect is pulled down and reads high with USB-C power present. */
  ESP_RETURN_ON_ERROR(
      esp_io_expander_set_pullupdown(e, EXP1_USB_C_VBUS, IO_EXPANDER_PULL_DOWN),
      TAG, "VBUS pull-down");
  return esp_io_expander_set_dir(e, EXP1_USB_C_VBUS, IO_EXPANDER_INPUT);
}

static esp_err_t init_backlight(void) {
  const ledc_timer_config_t timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = BACKLIGHT_RESOLUTION,
      .timer_num = CONFIG_M5_TAB5_BACKLIGHT_LEDC_TIMER,
      .freq_hz = 5000,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
  const ledc_channel_config_t channel = {
      .gpio_num = M5_TAB5_LCD_BACKLIGHT,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = CONFIG_M5_TAB5_BACKLIGHT_LEDC_CHANNEL,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = CONFIG_M5_TAB5_BACKLIGHT_LEDC_TIMER,
      .duty = 0,
      .hpoint = 0,
  };
  return ledc_channel_config(&channel);
}

esp_err_t m5_tab5_init(void) {
  if (initialized)
    return ESP_OK;
  if (!bus) {
    const i2c_master_bus_config_t config = {
        .i2c_port = CONFIG_M5_TAB5_I2C_PORT,
        .sda_io_num = M5_TAB5_I2C_SDA,
        .scl_io_num = M5_TAB5_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&config, &bus), TAG, "I2C bus");
  }
  static const uint32_t addresses[2] = {
      ESP_IO_EXPANDER_I2C_PI4IOE5V6408_ADDRESS_LOW,
      ESP_IO_EXPANDER_I2C_PI4IOE5V6408_ADDRESS_HIGH};
  for (int i = 0; i < 2; i++)
    if (!expanders[i])
      ESP_RETURN_ON_ERROR(esp_io_expander_new_i2c_pi4ioe5v6408(
                              bus, addresses[i], &expanders[i]),
                          TAG, "expander %d", i);
  ESP_RETURN_ON_ERROR(init_expander0(expanders[0]), TAG, "expander 0 setup");
  ESP_RETURN_ON_ERROR(init_expander1(expanders[1]), TAG, "expander 1 setup");
  ESP_RETURN_ON_ERROR(init_backlight(), TAG, "backlight");
  initialized = true;
  return ESP_OK;
}

esp_err_t m5_tab5_set_rail(m5_tab5_rail_t rail, bool on) {
  ESP_RETURN_ON_FALSE(initialized, ESP_ERR_INVALID_STATE, TAG, "not init");
  switch (rail) {
  case M5_TAB5_RAIL_SPEAKER:
    return esp_io_expander_set_level(expanders[0], EXP0_SPEAKER, on);
  case M5_TAB5_RAIL_EXT_5V:
    return esp_io_expander_set_level(expanders[0], EXP0_EXT_5V, on);
  case M5_TAB5_RAIL_CAMERA:
    return esp_io_expander_set_level(expanders[0], EXP0_CAMERA, on);
  case M5_TAB5_RAIL_WIFI:
    return esp_io_expander_set_level(expanders[1], EXP1_WIFI, on);
  case M5_TAB5_RAIL_USB_5V:
    return esp_io_expander_set_level(expanders[1], EXP1_USB_5V, on);
  case M5_TAB5_RAIL_QUICK_CHARGE:
    return esp_io_expander_set_level(expanders[1], EXP1_QUICK_CHARGE_N, !on);
  case M5_TAB5_RAIL_CHARGE:
    return esp_io_expander_set_level(expanders[1], EXP1_CHARGE, on);
  }
  return ESP_ERR_INVALID_ARG;
}

static esp_err_t read_input(esp_io_expander_handle_t expander, uint32_t pin,
                            bool *out) {
  ESP_RETURN_ON_FALSE(initialized && out, ESP_ERR_INVALID_ARG, TAG, "input");
  uint32_t levels = 0;
  ESP_RETURN_ON_ERROR(esp_io_expander_get_level(expander, pin, &levels), TAG,
                      "read");
  *out = (levels & pin) != 0;
  return ESP_OK;
}

esp_err_t m5_tab5_headphone_inserted(bool *out) {
  return read_input(expanders[0], EXP0_HEADPHONE, out);
}

esp_err_t m5_tab5_usb_c_powered(bool *out) {
  return read_input(expanders[1], EXP1_USB_C_VBUS, out);
}

esp_err_t m5_tab5_power_off(void) {
  ESP_RETURN_ON_FALSE(initialized, ESP_ERR_INVALID_STATE, TAG, "not init");
  for (int i = 0; i < 3; i++) {
    ESP_RETURN_ON_ERROR(
        esp_io_expander_set_level(expanders[1], EXP1_POWER_OFF, 1), TAG,
        "power-off pulse");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(
        esp_io_expander_set_level(expanders[1], EXP1_POWER_OFF, 0), TAG,
        "power-off pulse");
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return ESP_OK;
}

esp_err_t m5_tab5_backlight_set(uint32_t percent) {
  ESP_RETURN_ON_FALSE(initialized, ESP_ERR_INVALID_STATE, TAG, "not init");
  if (percent > 100)
    percent = 100;
  const uint32_t duty = BACKLIGHT_MAX_DUTY * percent / 100;
  ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE,
                                    CONFIG_M5_TAB5_BACKLIGHT_LEDC_CHANNEL,
                                    duty),
                      TAG, "duty");
  return ledc_update_duty(LEDC_LOW_SPEED_MODE,
                          CONFIG_M5_TAB5_BACKLIGHT_LEDC_CHANNEL);
}
