#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Identity section strings of the variant matching the host profile
 * (contracts/spec/pocket-package.ts, section kind 1). */
typedef struct {
  char output[64];
  char id[128];
  char title[96];
  uint64_t bytes;
} package_identity;

typedef struct {
  const uint8_t *profile_hash; /* 32 bytes */
  uint32_t host_abi;
} package_profile;

/* Each reader returns NULL on success or a static diagnostic. */
const char *package_identity_file(FILE *file, uint64_t size,
                                  const package_profile *profile,
                                  package_identity *out);
const char *package_identity_memory(const uint8_t *data, size_t size,
                                    const package_profile *profile,
                                    package_identity *out);
/* FNV-1a64 over every byte before the 8-byte footer. */
const char *package_verify_file(FILE *file, uint64_t size);
