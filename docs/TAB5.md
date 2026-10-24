# M5Stack Tab5

ESP32-P4 (silicon revision v1.x) with 32 MiB PSRAM and 16 MiB flash, a 5-inch
720×1280 MIPI-DSI panel and a capacitive touch controller. The PocketJS
profile `m5stack-tab5` uses the panel in its native portrait orientation:
**720×1280 physical, 360×640 logical, raster density 2, 60 Hz ticks.** Touch
contacts must fit the 10-bit frame encoding, so the logical viewport stays
below 1024 in each dimension.

## Panel variants

Board revisions differ by panel and touch controller. `m5_tab5_panel()`
identifies them on I2C after the touch reset release.

| Variant | LCD | Touch | Detection | DSI lane rate | DPI clock |
| --- | --- | --- | --- | --- | --- |
| 1 | ILI9881C | GT911 | ACK at 0x14 | 1000 Mbps | 60 MHz |
| 2 | ST7123 | ST7123 | 0x55, firmware byte 3 | 1000 Mbps | 70 MHz |
| 3 | ST7121 | ST712x | 0x55, firmware byte 1 | 965 Mbps | 70 MHz |

All variants use 2 data lanes and RGB565 input. LDO channel 3 supplies the DSI
PHY at 2.5 V.

## Pins and buses

| Signal | Connection |
| --- | --- |
| System I2C | SDA GPIO31, SCL GPIO32 (I2C port `CONFIG_M5_TAB5_I2C_PORT`) |
| Backlight | GPIO22, LEDC 5 kHz, 10-bit duty |
| Touch interrupt | GPIO23 |
| microSD (SDMMC slot 0, 4-bit) | CLK 43, CMD 44, D0 39, D1 40, D2 41, D3 42; IO power from LDO channel 4 |

System I2C devices: expanders 0x43 and 0x44, touch 0x14 or 0x55. The ES8388
and ES7210 codecs, BMI270 IMU, RX8130CE RTC and INA226 power monitor share
the bus and have no driver in this repository yet.

## IO expanders (PI4IOE5V6408)

| Expander | Pin | Function | State after `m5_tab5_init` |
| --- | --- | --- | --- |
| 0 (0x43) | P1 | speaker amplifier enable | output low |
| 0 | P2 | external 5 V output enable | output low |
| 0 | P4 | LCD reset | pulsed low 20 ms, then input with pull-up (the panel side has no 1.8 V pull-up) |
| 0 | P5 | touch reset | pulsed low 20 ms, then output high |
| 0 | P6 | camera enable | output low |
| 0 | P7 | headphone detect | input |
| 1 (0x44) | P0 | ESP32-C6 power | output low |
| 1 | P3 | USB-A host 5 V | output low |
| 1 | P4 | power-off request | output low; `m5_tab5_power_off` pulses it |
| 1 | P5 | quick charge, active low | low when `CONFIG_M5_TAB5_QUICK_CHARGE_ENABLE` |
| 1 | P6 | USB-C VBUS detect | input with pull-down |
| 1 | P7 | battery charge enable | high when `CONFIG_M5_TAB5_CHARGE_ENABLE` |

## Presentation path

The firmware creates the DPI panel with **two PSRAM frame buffers**
(2 × 1,843,200 bytes). The runtime renders into the buffer not in scanout,
with damage tracked per buffer, and the flush selects it for the next DMA
restart. With `CONFIG_TAB5_RENDERER_PPA` the PPA renderer performs fills,
A8 glyph blends and RGB565 scaling; other operations use the software
rasterizer. The panel refresh is 48.2 Hz (variant 1), 57.8 Hz (variant 2) or
57.3 Hz (variant 3) at the timings in `m5_bsp_tab5/src/display.c`, so
presentation is bounded by the panel rate.

## Not supported yet

Wi-Fi through the ESP32-C6, audio, camera, IMU, RTC, power monitor, USB host
and device, landscape orientation, and the flash-resident app catalog.

## Validation status

The firmware builds for ESP32-P4 with ESP-IDF 6.1. Device validation on each
panel variant is open: panel orientation (the ST7123 init table sets MADCTL
0x03), touch alignment, flip timing, SD install and app switching have not
been observed on hardware in this repository.
