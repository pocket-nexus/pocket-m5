#include "protocol.h"
#include <string.h>

int pocket_i2c_esp_decode(const uint8_t *data, size_t size,
                          size_t result_capacity, uint32_t max_bytes,
                          uint32_t max_timeout_ms, pocket_i2c_esp_script *out) {
  if (!data || !out || size < 4 || data[0] != 1 || data[1] == 0 ||
      data[1] > POCKET_I2C_ESP_STEPS)
    return 0;
  memset(out, 0, sizeof(*out));
  out->count = data[1];
  out->timeout_ms = (uint16_t)(data[2] | ((uint16_t)data[3] << 8));
  if (!out->timeout_ms || out->timeout_ms > max_timeout_ms)
    return 0;
  size_t offset = 4, transfer = 0, commands = 1;
  for (size_t i = 0; i < out->count; ++i) {
    if (size - offset < 4)
      return 0;
    pocket_i2c_esp_step *step = &out->steps[i];
    step->op = data[offset];
    step->reg = data[offset + 1];
    step->length =
        (uint16_t)(data[offset + 2] | ((uint16_t)data[offset + 3] << 8));
    offset += 4;
    if ((step->op == 1 && !step->length) || (step->op != 1 && step->op != 2))
      return 0;
    /* Include register and address bytes in the controller occupancy limit. */
    transfer += step->length + (step->op == 1 ? 3u : 2u);
    commands += step->op == 2 ? 2 : step->length > 1 ? 6 : 5;
    /* Supported IDF controllers have eight hardware command slots. */
    if (commands > 8)
      return 0;
    if (transfer > max_bytes)
      return 0;
    if (step->op == 1) {
      out->read_length += step->length;
      if (out->read_length > result_capacity)
        return 0;
    } else {
      if (step->length > size - offset)
        return 0;
      step->data = data + offset;
      offset += step->length;
    }
  }
  return offset == size;
}
