# m5_bsp_tab5

Board support for the M5Stack Tab5 (ESP32-P4). It owns the system I2C bus,
both PI4IOE5V6408 IO expanders, the switchable rails, panel identification,
the MIPI-DSI panel, the touch controller, the backlight and the microSD slot.
It does not depend on PocketJS; `programs/tab5` connects it to the PocketJS
drivers.

| Call | Effect |
| --- | --- |
| `m5_tab5_init` | I2C0 on GPIO31/32, expander reset, LCD/touch reset pulse, every rail off, Kconfig charging policy, backlight LEDC at duty 0 |
| `m5_tab5_panel` | Probes 0x55 (ST712x firmware 3 = ST7123, 1 = ST7121) then 0x14 (GT911 = ILI9881C) |
| `m5_tab5_display_new` | LDO3 at 2.5 V for the DSI PHY, 2-lane DSI bus, RGB565 DPI panel with 1–3 PSRAM frame buffers |
| `m5_tab5_touch_new` | GT911 or ST7123 touch handle in panel coordinates |
| `m5_tab5_sdcard_mount` | 4-bit SDMMC slot 0, LDO4 IO power, FAT mount |
| `m5_tab5_set_rail` | Speaker, external 5 V, camera, ESP32-C6, USB-A 5 V, charge, quick charge |

Pin and rail assignments are listed in [docs/TAB5.md](../../../docs/TAB5.md).

## Sources

- `src/panel_init_*.h` and the DPI timings in `src/display.c` come from
  `espressif/m5stack_tab5_noglib` 1.3.2 (esp-bsp, Apache-2.0). The headers
  keep their Espressif copyright and SPDX lines.
- The charging enables (expander 1 P5, P7) follow the M5Stack factory firmware
  `M5Tab5-UserDemo` (`bsp_set_charge_en`, `bsp_set_charge_qc_en`).
- Chip drivers come from the ESP component registry at the versions pinned in
  `idf_component.yml`: `esp_io_expander_pi4ioe5v6408`, `esp_lcd_ili9881c`,
  `esp_lcd_st7123`, `esp_lcd_touch_gt911`, `esp_lcd_touch_st7123`.

## Constraints

- The DSI interrupts are allocated on the core that calls
  `m5_tab5_display_new`. `pocket_display_esp_dpi_flip` requires that core to run
  the runtime owner task.
- `m5_tab5_panel` waits until 500 ms after the touch reset release before the
  first probe.
- The GT911 variant keeps GPIO23 driven low after the address latch; its
  touch driver runs without an interrupt pin.
