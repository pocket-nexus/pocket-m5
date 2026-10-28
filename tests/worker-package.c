#include "package_info.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put32(uint8_t *data, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    data[i] = (uint8_t)(value >> (i * 8));
}
int main(void) {
  uint8_t data[512] = {0};
  uint8_t hash[32] = {42};
  const package_profile profile = {.profile_hash = hash,
                                   .host_abi = 7,
                                   .target = (const uint8_t *)"tab",
                                   .target_length = 3};
  put32(data, 0x544b4350);
  put32(data + 4, 2);
  put32(data + 12, 1);
  put32(data + 32, 224);
  put32(data + 32 + 4, 3);
  memcpy(data + 224, "tab", 3);
  put32(data + 32 + 8, 7);
  put32(data + 32 + 12, 1);
  put32(data + 32 + 16, 96);
  memcpy(data + 64, hash, 32);
  put32(data + 96, 8);
  put32(data + 104, 144);
  put32(data + 108, 4);
  memcpy(data + 144, "x=1", 4);
  const char *source;
  size_t length;
  assert(
      !package_worker_memory(data, sizeof(data), &profile, &source, &length));
  assert(source == (const char *)data + 144 && length == 3);
  data[147] = 'x';
  assert(package_worker_memory(data, sizeof(data), &profile, &source, &length));
  data[147] = 0;
  put32(data + 108, 0);
  assert(package_worker_memory(data, sizeof(data), &profile, &source, &length));
  put32(data + 108, 4);
  put32(data + 104, 510);
  assert(package_worker_memory(data, sizeof(data), &profile, &source, &length));
  put32(data + 104, 144);
  put32(data + 32 + 12, 2);
  memcpy(data + 112, data + 96, 16);
  assert(package_worker_memory(data, sizeof(data), &profile, &source, &length));
  put32(data + 32 + 12, 0);
  assert(
      !package_worker_memory(data, sizeof(data), &profile, &source, &length));
  assert(!source && !length);
  data[64] ^= 1;
  assert(package_worker_memory(data, sizeof(data), &profile, &source, &length));
  assert(package_worker_memory(data, 32, &profile, &source, &length));
  /* An earlier variant can share ABI/hash while naming another target. */
  data[64] ^= 1;
  put32(data + 12, 2);
  memcpy(data + 96, data + 32, 64);
  put32(data + 32, 228);
  memcpy(data + 228, "bad", 3);
  put32(data + 32 + 12, 1);
  put32(data + 32 + 16, 160);
  put32(data + 96 + 12, 1);
  put32(data + 96 + 16, 176);
  put32(data + 160, 8);
  put32(data + 168, 200);
  put32(data + 172, 4);
  put32(data + 176, 8);
  put32(data + 184, 208);
  put32(data + 188, 4);
  memcpy(data + 200, "bad", 4);
  memcpy(data + 208, "yes", 4);
  assert(
      !package_worker_memory(data, sizeof(data), &profile, &source, &length));
  assert(length == 3 && !memcmp(source, "yes", 3));
  put32(data + 96 + 12, 0);
  assert(
      !package_worker_memory(data, sizeof(data), &profile, &source, &length));
  assert(!source && !length);
  put32(data + 96, 510);
  assert(package_worker_memory(data, sizeof(data), &profile, &source, &length));
  puts("worker package source validation: passed");
}
