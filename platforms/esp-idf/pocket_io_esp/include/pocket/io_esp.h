#pragma once
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "pocket/worker.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pocket_i2c_esp pocket_i2c_esp;
typedef struct pocket_uart_esp pocket_uart_esp;

typedef struct {
  uint32_t struct_size;
  i2c_master_bus_handle_t bus;
  /* The firmware grants these addresses to this worker. Board-native owners
   * must be excluded. The list is copied at creation. */
  const uint8_t *addresses;
  size_t address_count;
  uint32_t speed_hz, max_transaction_bytes, max_timeout_ms;
  int task_core;
} pocket_i2c_esp_config;

pocket_i2c_esp *pocket_i2c_esp_create(const pocket_i2c_esp_config *config);
const pocket_worker_driver *pocket_i2c_esp_driver(pocket_i2c_esp *driver);
/* Call after the worker core has closed all handles and stopped producers. */
int32_t pocket_i2c_esp_destroy(pocket_i2c_esp *driver);

typedef struct {
  uint32_t struct_size;
  uart_port_t port;
  int tx_pin, rx_pin, rts_pin, cts_pin;
  uint32_t max_baud;
  bool loopback;
  int task_core;
} pocket_uart_esp_config;

pocket_uart_esp *pocket_uart_esp_create(const pocket_uart_esp_config *config);
const pocket_worker_driver *pocket_uart_esp_driver(pocket_uart_esp *driver);
int32_t pocket_uart_esp_destroy(pocket_uart_esp *driver);

#ifdef __cplusplus
}
#endif
