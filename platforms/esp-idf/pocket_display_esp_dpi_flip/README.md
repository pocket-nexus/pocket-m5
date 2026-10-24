# pocket_display_esp_dpi_flip

PocketJS display output (`pocket_display_output_v1`) over an esp_lcd MIPI-DSI
DPI panel with **two RGB565 frame buffers**. The runtime renders into the
buffer that is not in scanout; flush hands that buffer to scanout.

## Presentation

- `frame_identity` reports the back buffer as `buffer = index + 1`, so the
  runtime keeps one damage history per frame buffer. `invalidate` advances the
  generation of both.
- `acquire` returns the full-width back buffer region. If the previous flush
  has not reached scanout yet, it waits for the DPI frame-complete interrupt,
  bounded by `flip_timeout_ms`; a timeout returns `POCKET_STATUS_FAILED`.
- `flush` calls `esp_lcd_panel_draw_bitmap` once per damage region with a
  pointer inside the frame buffer. esp_lcd writes back the cached rows and
  selects that buffer for the next DMA restart. No pixels are copied.
- `wait` returns after flush's cache write-back. Scanout keeps reading the
  flushed buffer until a later flip; no writer touches it before the next
  `acquire` of that buffer, which waits for the flip away from it.

The descriptor declares `DOUBLE_BUFFERED | PARTIAL_REFRESH` with 1×1 refresh
granularity and alignment. `pocket_display_esp_dpi_flip_budget` returns the
`frame_buffer_budget` the runtime needs: `width × height × 2 × 2` bytes.

## Requirements

- Create the panel with `num_fbs = 2` and `in_color_format =
  LCD_COLOR_FMT_RGB565`, and do not draw to it outside this driver.
- Create the panel on the core that runs the runtime owner task. The flip
  bookkeeping reads the frame counter after the buffer index switch; it relies
  on the frame-complete interrupt not running in parallel with flush.
- The driver state lives in internal RAM and the callback is in IRAM, so
  `CONFIG_LCD_DSI_ISR_CACHE_SAFE` is supported.
