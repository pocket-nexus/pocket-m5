#pragma once
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_io_expander.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "sdmmc_cmd.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Panel geometry in its native portrait scan order. */
#define M5_TAB5_LCD_WIDTH 720u
#define M5_TAB5_LCD_HEIGHT 1280u

/* System I2C carries both IO expanders, touch, codecs, IMU, RTC and the power
 * monitor. */
#define M5_TAB5_I2C_SDA GPIO_NUM_31
#define M5_TAB5_I2C_SCL GPIO_NUM_32
#define M5_TAB5_LCD_BACKLIGHT GPIO_NUM_22
#define M5_TAB5_TOUCH_INT GPIO_NUM_23
#define M5_TAB5_SD_CLK GPIO_NUM_43
#define M5_TAB5_SD_CMD GPIO_NUM_44
#define M5_TAB5_SD_D0 GPIO_NUM_39
#define M5_TAB5_SD_D1 GPIO_NUM_40
#define M5_TAB5_SD_D2 GPIO_NUM_41
#define M5_TAB5_SD_D3 GPIO_NUM_42

/* PI4IOE5V6408 expanders: 0 at I2C 0x43, 1 at 0x44. */
typedef enum {
  M5_TAB5_EXPANDER_0 = 0,
  M5_TAB5_EXPANDER_1 = 1,
} m5_tab5_expander_t;

/* Switchable rails and enables behind the expanders. All start off except the
 * charging rails, which follow Kconfig. */
typedef enum {
  M5_TAB5_RAIL_SPEAKER,      /* expander 0 P1: speaker amplifier */
  M5_TAB5_RAIL_EXT_5V,       /* expander 0 P2: 5 V on the expansion ports */
  M5_TAB5_RAIL_CAMERA,       /* expander 0 P6: camera module */
  M5_TAB5_RAIL_WIFI,         /* expander 1 P0: ESP32-C6 radio */
  M5_TAB5_RAIL_USB_5V,       /* expander 1 P3: USB-A host 5 V */
  M5_TAB5_RAIL_QUICK_CHARGE, /* expander 1 P5 (active low): quick charge */
  M5_TAB5_RAIL_CHARGE,       /* expander 1 P7: battery charging */
} m5_tab5_rail_t;

/* Board revisions differ by LCD and touch controller. */
typedef enum {
  M5_TAB5_PANEL_UNKNOWN = 0,
  M5_TAB5_PANEL_ILI9881C, /* LCD ILI9881C, touch GT911 at 0x14 */
  M5_TAB5_PANEL_ST7123,   /* LCD and touch ST7123 at 0x55, firmware 3 */
  M5_TAB5_PANEL_ST7121,   /* LCD ST7121, touch at 0x55, firmware 1 */
} m5_tab5_panel_t;

typedef struct {
  m5_tab5_panel_t kind;
  esp_lcd_dsi_bus_handle_t bus;
  esp_lcd_panel_io_handle_t io;
  esp_lcd_panel_handle_t panel;
} m5_tab5_display_t;

/* Creates the system I2C bus, resets both expanders, pulses the LCD and touch
 * resets, sets every rail off and applies the Kconfig charging policy. Later
 * calls return ESP_OK without repeating the sequence. */
esp_err_t m5_tab5_init(void);
i2c_master_bus_handle_t m5_tab5_i2c(void);
esp_io_expander_handle_t m5_tab5_expander(m5_tab5_expander_t expander);
esp_err_t m5_tab5_set_rail(m5_tab5_rail_t rail, bool on);
/* Expander 0 P7 reads the headphone jack; expander 1 P6 reads USB-C VBUS. */
esp_err_t m5_tab5_headphone_inserted(bool *out);
esp_err_t m5_tab5_usb_c_powered(bool *out);
/* Pulses expander 1 P4, the power-path controller's power-off request. */
esp_err_t m5_tab5_power_off(void);

/* Identifies the panel from the touch controller on I2C. The first call waits
 * until 500 ms after the touch reset release; the result is cached. */
m5_tab5_panel_t m5_tab5_panel(void);
const char *m5_tab5_panel_name(m5_tab5_panel_t kind);

/* RGB565 DPI panel with fb_count (1..3) screen-sized PSRAM frame buffers. The
 * panel scans out frame buffer 0 after return. The DSI interrupts are
 * allocated on the calling core. */
esp_err_t m5_tab5_display_new(uint32_t fb_count, m5_tab5_display_t *out);
void m5_tab5_display_del(m5_tab5_display_t *display);
/* LEDC duty on GPIO22; 0 turns the backlight off. Values above 100 clamp. */
esp_err_t m5_tab5_backlight_set(uint32_t percent);

/* Touch controller matching m5_tab5_panel(), reporting panel coordinates in
 * 0..719 x 0..1279. The caller owns the handle. */
esp_err_t m5_tab5_touch_new(esp_lcd_touch_handle_t *out);

/* 4-bit SDMMC slot 0 powered by on-chip LDO channel 4, FAT mounted at
 * mount_point. The card is not formatted on mount failure. */
esp_err_t m5_tab5_sdcard_mount(const char *mount_point, size_t max_files,
                               sdmmc_card_t **out);
esp_err_t m5_tab5_sdcard_unmount(const char *mount_point, sdmmc_card_t *card);

#ifdef __cplusplus
}
#endif
