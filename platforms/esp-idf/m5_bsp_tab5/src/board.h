#pragma once
#include <stdint.h>

/* esp_timer time of the LCD and touch reset release; zero before init. */
int64_t m5_tab5_reset_released_us(void);
