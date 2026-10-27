#pragma once
#include "pocket/app_host.h"

/* Program-lifetime wrappers for opt-in USB JTAG validation. Call before the
 * owner starts; no runtime or generic driver ABI is changed. */
esp_err_t tab5_validation_install(pocket_app_host_config *host);
