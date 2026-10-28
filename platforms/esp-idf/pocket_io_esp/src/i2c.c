#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pocket/io_esp.h"
#include "protocol.h"
#include <stdlib.h>
#include <string.h>

#define DEVICE_COUNT 8
#define REQUEST_COUNT 8

typedef struct {
  uint8_t address;
  uint32_t handle;
  i2c_master_dev_handle_t device;
  unsigned active;
  bool closing;
} device_slot;
typedef struct {
  bool used, cancelled, started;
  uint32_t request;
  pocket_worker *worker;
  device_slot *device;
  size_t length, capacity;
  uint8_t data[POCKET_WORKER_MAX_PAYLOAD];
} request_slot;
struct pocket_i2c_esp {
  pocket_worker_driver vtable;
  pocket_i2c_esp_config config;
  SemaphoreHandle_t mutex, stopped;
  QueueHandle_t queue;
  device_slot devices[DEVICE_COUNT];
  request_slot requests[REQUEST_COUNT];
  bool stopping;
};

/* A driver instance reserves its firmware-granted addresses across all
 * worker sessions; two instances cannot configure the same physical device. */
static portMUX_TYPE claims_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
  i2c_master_bus_handle_t bus;
  uint8_t address;
  pocket_i2c_esp *owner;
} claims[32];
static bool reserve_claims(pocket_i2c_esp *owner,
                           const pocket_i2c_esp_config *config) {
  bool accepted = true;
  size_t available = 0;
  portENTER_CRITICAL(&claims_lock);
  for (size_t i = 0; i < 32; ++i) {
    if (!claims[i].owner) {
      ++available;
      continue;
    }
    for (size_t j = 0; j < config->address_count; ++j)
      if (claims[i].bus == config->bus &&
          claims[i].address == config->addresses[j])
        accepted = false;
  }
  if (available < config->address_count)
    accepted = false;
  if (accepted) {
    size_t next = 0;
    for (size_t i = 0; i < 32 && next < config->address_count; ++i) {
      if (claims[i].owner)
        continue;
      claims[i].bus = config->bus;
      claims[i].address = config->addresses[next++];
      claims[i].owner = owner;
    }
  }
  portEXIT_CRITICAL(&claims_lock);
  return accepted;
}
static void release_claims(pocket_i2c_esp *owner) {
  portENTER_CRITICAL(&claims_lock);
  for (size_t i = 0; i < 32; ++i)
    if (claims[i].owner == owner)
      claims[i].owner = NULL;
  portEXIT_CRITICAL(&claims_lock);
}

static device_slot *lookup(pocket_i2c_esp *driver, uint32_t handle) {
  for (size_t i = 0; i < driver->config.address_count; ++i)
    if (driver->devices[i].handle == handle)
      return &driver->devices[i];
  return NULL;
}

static int32_t driver_open(void *opaque, pocket_worker *worker, uint32_t handle,
                           const uint8_t *bytes, size_t length) {
  (void)worker;
  pocket_i2c_esp *driver = opaque;
  if (!bytes || length != 2 || bytes[0] != 1)
    return POCKET_WORKER_INVALID;
  int32_t status = POCKET_WORKER_INVALID;
  xSemaphoreTake(driver->mutex, portMAX_DELAY);
  for (size_t i = 0; i < driver->config.address_count; ++i) {
    device_slot *device = &driver->devices[i];
    if (device->address != bytes[1])
      continue;
    status = POCKET_WORKER_BUSY;
    if (!driver->stopping && device->handle == 0) {
      device->handle = handle;
      device->closing = false;
      status = POCKET_WORKER_OK;
    }
    break;
  }
  xSemaphoreGive(driver->mutex);
  return status;
}

static int32_t driver_validate(void *opaque, pocket_worker *worker,
                               uint32_t handle, uint32_t opcode,
                               const uint8_t *bytes, size_t length,
                               size_t capacity) {
  (void)worker;
  (void)handle;
  pocket_i2c_esp *driver = opaque;
  pocket_i2c_esp_script script;
  return opcode == 1 && length <= POCKET_WORKER_MAX_PAYLOAD &&
                 pocket_i2c_esp_decode(bytes, length, capacity,
                                       driver->config.max_transaction_bytes,
                                       driver->config.max_timeout_ms, &script)
             ? POCKET_WORKER_OK
             : POCKET_WORKER_INVALID;
}

static int32_t driver_start(void *opaque, pocket_worker *worker,
                            uint32_t handle, uint32_t request, uint32_t opcode,
                            const uint8_t *bytes, size_t length,
                            size_t result_capacity) {
  pocket_i2c_esp *driver = opaque;
  pocket_i2c_esp_script script;
  if (opcode != 1 || length > POCKET_WORKER_MAX_PAYLOAD ||
      !pocket_i2c_esp_decode(bytes, length, result_capacity,
                             driver->config.max_transaction_bytes,
                             driver->config.max_timeout_ms, &script))
    return POCKET_WORKER_INVALID;
  int32_t status = POCKET_WORKER_BUSY;
  xSemaphoreTake(driver->mutex, portMAX_DELAY);
  device_slot *device = lookup(driver, handle);
  if (device && !device->closing && !driver->stopping) {
    for (size_t i = 0; i < REQUEST_COUNT; ++i) {
      request_slot *slot = &driver->requests[i];
      if (slot->used)
        continue;
      *slot = (request_slot){.used = true,
                             .request = request,
                             .worker = worker,
                             .device = device,
                             .length = length,
                             .capacity = result_capacity};
      memcpy(slot->data, bytes, length);
      ++device->active;
      if (xQueueSend(driver->queue, &slot, 0) == pdTRUE) {
        status = POCKET_WORKER_OK;
      } else {
        slot->used = false;
        --device->active;
      }
      break;
    }
  }
  xSemaphoreGive(driver->mutex);
  return status;
}

/* Each descriptor becomes one IDF operation list, so the IDF bus semaphore
 * covers every repeated START through the final STOP, including board-native
 * transfers submitted through the same bus handle. */
static esp_err_t execute(request_slot *slot, pocket_i2c_esp_script *script,
                         uint8_t *result) {
  i2c_operation_job_t ops[8];
  uint8_t address_read[POCKET_I2C_ESP_STEPS];
  uint8_t transmit[512 + POCKET_I2C_ESP_STEPS * 2];
  size_t count = 0, read_offset = 0, tx_offset = 0;
  for (size_t i = 0; i < script->count; ++i) {
    pocket_i2c_esp_step *step = &script->steps[i];
    const size_t tx_start = tx_offset;
    transmit[tx_offset++] = slot->device->address << 1;
    transmit[tx_offset++] = step->reg;
    if (step->op == 2) {
      memcpy(transmit + tx_offset, step->data, step->length);
      tx_offset += step->length;
    }
    ops[count++] = (i2c_operation_job_t){.command = I2C_MASTER_CMD_START};
    ops[count++] =
        (i2c_operation_job_t){.command = I2C_MASTER_CMD_WRITE,
                              .write = {.ack_check = true,
                                        .data = transmit + tx_start,
                                        .total_bytes = tx_offset - tx_start}};
    if (step->op == 1) {
      address_read[i] = (slot->device->address << 1) | 1;
      ops[count++] = (i2c_operation_job_t){.command = I2C_MASTER_CMD_START};
      ops[count++] = (i2c_operation_job_t){.command = I2C_MASTER_CMD_WRITE,
                                           .write = {.ack_check = true,
                                                     .data = &address_read[i],
                                                     .total_bytes = 1}};
      if (step->length > 1)
        ops[count++] =
            (i2c_operation_job_t){.command = I2C_MASTER_CMD_READ,
                                  .read = {.ack_value = I2C_ACK_VAL,
                                           .data = result + read_offset,
                                           .total_bytes = step->length - 1}};
      ops[count++] = (i2c_operation_job_t){
          .command = I2C_MASTER_CMD_READ,
          .read = {.ack_value = I2C_NACK_VAL,
                   .data = result + read_offset + step->length - 1,
                   .total_bytes = 1}};
      read_offset += step->length;
    }
  }
  ops[count++] = (i2c_operation_job_t){.command = I2C_MASTER_CMD_STOP};
  return i2c_master_execute_defined_operations(slot->device->device, ops, count,
                                               script->timeout_ms);
}

static void executor(void *opaque) {
  pocket_i2c_esp *driver = opaque;
  request_slot *slot;
  while (xQueueReceive(driver->queue, &slot, portMAX_DELAY) == pdTRUE && slot) {
    xSemaphoreTake(driver->mutex, portMAX_DELAY);
    const bool cancelled = slot->cancelled || slot->device->closing;
    slot->started = !cancelled;
    xSemaphoreGive(driver->mutex);
    int32_t status = POCKET_WORKER_CANCELLED;
    uint8_t result[POCKET_WORKER_MAX_PAYLOAD];
    size_t length = 0;
    if (!cancelled) {
      pocket_i2c_esp_script script;
      if (pocket_i2c_esp_decode(slot->data, slot->length, slot->capacity,
                                driver->config.max_transaction_bytes,
                                driver->config.max_timeout_ms, &script)) {
        const esp_err_t err = execute(slot, &script, result);
        /* IDF cannot report which writes took effect before a NACK/timeout. */
        status = err == ESP_OK ? POCKET_WORKER_OK : POCKET_WORKER_UNKNOWN;
        if (status == POCKET_WORKER_OK)
          length = script.read_length;
      } else {
        status = POCKET_WORKER_INVALID;
      }
    }
    xSemaphoreTake(driver->mutex, portMAX_DELAY);
    if (slot->started && slot->cancelled) {
      status = POCKET_WORKER_UNKNOWN;
      length = 0;
    }
    const uint32_t request = slot->request;
    pocket_worker *worker = slot->worker;
    /* Keep active set until complete has returned: close cannot reclaim the
     * worker while the executor is still publishing its completion. */
    xSemaphoreGive(driver->mutex);
    pocket_worker_complete(worker, request, status, result, length);
    xSemaphoreTake(driver->mutex, portMAX_DELAY);
    --slot->device->active;
    slot->used = false;
    xSemaphoreGive(driver->mutex);
  }
  xSemaphoreGive(driver->stopped);
  vTaskDelete(NULL);
}

static void driver_cancel(void *opaque, pocket_worker *worker, uint32_t handle,
                          uint32_t request) {
  (void)worker;
  (void)handle;
  pocket_i2c_esp *driver = opaque;
  xSemaphoreTake(driver->mutex, portMAX_DELAY);
  for (size_t i = 0; i < REQUEST_COUNT; ++i)
    if (driver->requests[i].used && driver->requests[i].request == request)
      driver->requests[i].cancelled = true;
  xSemaphoreGive(driver->mutex);
}

static int32_t driver_close(void *opaque, pocket_worker *worker,
                            uint32_t handle) {
  (void)worker;
  pocket_i2c_esp *driver = opaque;
  int32_t status = POCKET_WORKER_OK;
  xSemaphoreTake(driver->mutex, portMAX_DELAY);
  device_slot *device = lookup(driver, handle);
  if (device) {
    device->closing = true;
    if (device->active)
      status = POCKET_WORKER_BUSY;
    else
      device->handle = 0;
  }
  xSemaphoreGive(driver->mutex);
  return status;
}

pocket_i2c_esp *pocket_i2c_esp_create(const pocket_i2c_esp_config *config) {
  if (!config || config->struct_size < sizeof(*config) || !config->bus ||
      !config->addresses || !config->address_count ||
      config->address_count > DEVICE_COUNT || config->speed_hz < 10000 ||
      config->speed_hz > 400000 || !config->max_transaction_bytes ||
      config->max_transaction_bytes > 512 || !config->max_timeout_ms ||
      config->max_timeout_ms > 100)
    return NULL;
  for (size_t i = 0; i < config->address_count; ++i) {
    if (config->addresses[i] < 0x08 || config->addresses[i] > 0x77)
      return NULL;
    for (size_t j = 0; j < i; ++j)
      if (config->addresses[j] == config->addresses[i])
        return NULL;
  }
  pocket_i2c_esp *driver = calloc(1, sizeof(*driver));
  if (!driver)
    return NULL;
  if (!reserve_claims(driver, config)) {
    free(driver);
    return NULL;
  }
  driver->config = *config;
  driver->config.addresses = NULL;
  driver->mutex = xSemaphoreCreateMutex();
  driver->stopped = xSemaphoreCreateBinary();
  driver->queue = xQueueCreate(REQUEST_COUNT, sizeof(request_slot *));
  if (!driver->mutex || !driver->stopped || !driver->queue)
    goto failed;
  for (size_t i = 0; i < config->address_count; ++i) {
    driver->devices[i].address = config->addresses[i];
    const i2c_device_config_t device = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                        .device_address =
                                            I2C_DEVICE_ADDRESS_NOT_USED,
                                        .scl_speed_hz = config->speed_hz};
    if (i2c_master_bus_add_device(config->bus, &device,
                                  &driver->devices[i].device) != ESP_OK)
      goto failed;
  }
  driver->vtable = (pocket_worker_driver){.struct_size = sizeof(driver->vtable),
                                          .context = driver,
                                          .open = driver_open,
                                          .start = driver_start,
                                          .validate = driver_validate,
                                          .cancel = driver_cancel,
                                          .close = driver_close};
  if (xTaskCreatePinnedToCore(executor, "pocket_i2c", 6144, driver, 4, NULL,
                              config->task_core) != pdPASS)
    goto failed;
  return driver;
failed:
  for (size_t i = 0; i < config->address_count; ++i)
    if (driver->devices[i].device)
      i2c_master_bus_rm_device(driver->devices[i].device);
  if (driver->queue)
    vQueueDelete(driver->queue);
  if (driver->stopped)
    vSemaphoreDelete(driver->stopped);
  if (driver->mutex)
    vSemaphoreDelete(driver->mutex);
  release_claims(driver);
  free(driver);
  return NULL;
}

const pocket_worker_driver *pocket_i2c_esp_driver(pocket_i2c_esp *driver) {
  return driver ? &driver->vtable : NULL;
}

int32_t pocket_i2c_esp_destroy(pocket_i2c_esp *driver) {
  if (!driver)
    return POCKET_WORKER_OK;
  xSemaphoreTake(driver->mutex, portMAX_DELAY);
  for (size_t i = 0; i < driver->config.address_count; ++i) {
    if (driver->devices[i].handle || driver->devices[i].active) {
      xSemaphoreGive(driver->mutex);
      return POCKET_WORKER_BUSY;
    }
  }
  driver->stopping = true;
  xSemaphoreGive(driver->mutex);
  request_slot *stop = NULL;
  xQueueSend(driver->queue, &stop, portMAX_DELAY);
  xSemaphoreTake(driver->stopped, portMAX_DELAY);
  for (size_t i = 0; i < driver->config.address_count; ++i)
    i2c_master_bus_rm_device(driver->devices[i].device);
  vQueueDelete(driver->queue);
  vSemaphoreDelete(driver->stopped);
  vSemaphoreDelete(driver->mutex);
  release_claims(driver);
  free(driver);
  return POCKET_WORKER_OK;
}
