#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_vfs_fat.h"
#include "m5/tab5.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

static const char *TAG = "m5_tab5_sd";

#define SD_LDO_CHANNEL 4 /* LDO_VO4 feeds the SDMMC IO bank */

static sd_pwr_ctrl_handle_t power;

esp_err_t m5_tab5_sdcard_mount(const char *mount_point, size_t max_files,
                               sdmmc_card_t **out) {
  ESP_RETURN_ON_FALSE(mount_point && out && max_files, ESP_ERR_INVALID_ARG, TAG,
                      "arguments");
  *out = NULL;
  if (!power) {
    const sd_pwr_ctrl_ldo_config_t ldo = {.ldo_chan_id = SD_LDO_CHANNEL};
    ESP_RETURN_ON_ERROR(sd_pwr_ctrl_new_on_chip_ldo(&ldo, &power), TAG,
                        "SD LDO");
  }
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
  host.pwr_ctrl_handle = power;
  /* Slot 0 uses its IO MUX pins; the fields document the routing. */
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 4;
  slot.clk = M5_TAB5_SD_CLK;
  slot.cmd = M5_TAB5_SD_CMD;
  slot.d0 = M5_TAB5_SD_D0;
  slot.d1 = M5_TAB5_SD_D1;
  slot.d2 = M5_TAB5_SD_D2;
  slot.d3 = M5_TAB5_SD_D3;
  const esp_vfs_fat_sdmmc_mount_config_t mount = {
      .format_if_mount_failed = false,
      .max_files = (int)max_files,
      .allocation_unit_size = 16 * 1024,
  };
  return esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot, &mount, out);
}

esp_err_t m5_tab5_sdcard_unmount(const char *mount_point, sdmmc_card_t *card) {
  ESP_RETURN_ON_FALSE(mount_point && card, ESP_ERR_INVALID_ARG, TAG,
                      "arguments");
  return esp_vfs_fat_sdcard_unmount(mount_point, card);
}
