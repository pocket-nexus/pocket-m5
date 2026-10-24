/* M5Stack Tab5 firmware: board bring-up, PocketJS drivers and the multi-app
 * host. app_main runs on CPU0; the DSI panel and the host task share it. */
#include "esp_log.h"
#include "m5/tab5.h"
#include "pocket/app_host.h"
#include "pocket/display_esp_dpi_flip.h"
#include "pocket/input_esp_touch.h"
#include "pocket_profile.h"
#include "pocketjs_package_home.h"
#include "sdkconfig.h"
#if CONFIG_TAB5_RENDERER_PPA
#include "pocketjs/esp32p4_ppa_status.h"
#endif

_Static_assert(POCKET_PROFILE_WIDTH == M5_TAB5_LCD_WIDTH &&
                   POCKET_PROFILE_HEIGHT == M5_TAB5_LCD_HEIGHT,
               "profile physical viewport must match the panel");

static const char *TAG = "tab5";

#define SD_MOUNT "/sdcard"

#if CONFIG_TAB5_RENDERER_PPA
static const pocket_renderer_v1 *create_renderer(void **context) {
  pocketjs_esp32p4_ppa_t *device = NULL;
  ESP_ERROR_CHECK(pocketjs_esp32p4_ppa_create(&device));
  const pocket_ppa_config config = {
      .struct_size = sizeof(config),
      .scale = pocket_profile.facts->display->raster_density,
      .min_fill_pixels = 1024,
      .min_blend_pixels = 256,
      .min_srm_pixels = 256,
      .mask_budget = 256 * 1024,
  };
  pocket_render_esp_ppa *renderer = pocket_render_esp_ppa_create(
      &config, pocketjs_esp32p4_ppa_status_ops(device));
  if (!renderer) {
    ESP_LOGE(TAG, "PPA renderer unavailable; using software rendering");
    pocketjs_esp32p4_ppa_destroy(device);
    return NULL;
  }
  *context = renderer;
  return pocket_render_esp_ppa_vtable();
}
#endif

void app_main(void) {
  ESP_ERROR_CHECK(m5_tab5_init());

  m5_tab5_display_t lcd;
  ESP_ERROR_CHECK(m5_tab5_display_new(2, &lcd));
  const pocket_display_esp_dpi_flip_config display_config = {
      .struct_size = sizeof(display_config),
      .panel = lcd.panel,
      .width = M5_TAB5_LCD_WIDTH,
      .height = M5_TAB5_LCD_HEIGHT,
      .flip_timeout_ms = 200,
  };
  pocket_display_esp_dpi_flip *display =
      pocket_display_esp_dpi_flip_create(&display_config);
  if (!display) {
    ESP_LOGE(TAG, "display driver creation failed");
    return;
  }
  ESP_ERROR_CHECK(m5_tab5_backlight_set(CONFIG_TAB5_BACKLIGHT_PERCENT));

  esp_lcd_touch_handle_t touch_handle = NULL;
  ESP_ERROR_CHECK(m5_tab5_touch_new(&touch_handle));
  const float density = (float)pocket_profile.facts->display->raster_density;
  const pocket_input_esp_touch_config touch_config = {
      .struct_size = sizeof(touch_config),
      .touch = touch_handle,
      .surface = 0,
      .scale_x = 1.0f / density,
      .scale_y = 1.0f / density,
      .match_radius = 48,
  };
  pocket_input_esp_touch *touch = pocket_input_esp_touch_create(&touch_config);
  if (!touch) {
    ESP_LOGE(TAG, "touch driver creation failed");
    return;
  }

  sdmmc_card_t *card = NULL;
  const esp_err_t sd = m5_tab5_sdcard_mount(SD_MOUNT, 8, &card);
  if (sd != ESP_OK)
    ESP_LOGW(TAG, "SD card unavailable (%s); only home runs",
             esp_err_to_name(sd));

  pocket_app_host_config host;
  pocket_app_host_config_defaults(&host);
  host.profile = pocket_profile.facts;
  host.profile_hash = pocket_profile.profile_hash;
  host.host_abi = pocket_profile.host_abi;
  host.tick_hz = pocket_profile.tick_hz;
  host.runtime_heap_limit = CONFIG_TAB5_RUNTIME_HEAP_KB * 1024u;
  host.guest_heap_limit = CONFIG_TAB5_GUEST_HEAP_KB * 1024u;
  host.frame_buffer_budget = pocket_display_esp_dpi_flip_budget(display);
  host.display = pocket_display_esp_dpi_flip_vtable();
  host.display_context = display;
  host.input = pocket_input_esp_touch_vtable();
  host.input_context = touch;
#if CONFIG_TAB5_RENDERER_PPA
  host.renderer = create_renderer(&host.renderer_context);
#endif
  host.home_package = pocketjs_package_home.data;
  host.home_package_size = pocketjs_package_home.size;
  host.apps_dir = sd == ESP_OK ? SD_MOUNT "/pocket/apps" : NULL;
  host.inbox_dir = sd == ESP_OK ? SD_MOUNT "/pocket/inbox" : NULL;
  host.task_core = 0;

  pocket_app_host *app_host = NULL;
  ESP_ERROR_CHECK(pocket_app_host_start(&host, &app_host));
  ESP_LOGI(TAG, "panel %s, host started", m5_tab5_panel_name(lcd.kind));
}
