#include "package_info.h"
#include <string.h>

/* Layout constants from contracts/spec/pocket-package.ts, format version 2. */
#define PACKAGE_MAGIC 0x544b4350u /* "PCKT" */
#define PACKAGE_VERSION 2u
#define HEADER_SIZE 32u
#define VARIANT_SIZE 64u
#define SECTION_SIZE 16u
#define FOOTER_SIZE 8u
#define SECTION_IDENTITY 1u
#define MAX_VARIANTS 64u
#define MAX_SECTIONS 64u
#define MAX_IDENTITY 512u

typedef struct {
  bool (*read)(void *context, uint64_t offset, void *out, size_t length);
  void *context;
  uint64_t size;
} reader;

static uint32_t u32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}

static bool within(const reader *r, uint64_t offset, uint64_t length) {
  const uint64_t payload = r->size - FOOTER_SIZE;
  return offset <= payload && length <= payload - offset;
}

/* output names a file on FAT storage: [A-Za-z0-9._-], no leading dot. */
static bool valid_output(const char *text) {
  if (!text[0] || text[0] == '.')
    return false;
  for (const char *c = text; *c; c++)
    if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
          (*c >= '0' && *c <= '9') || *c == '.' || *c == '_' || *c == '-'))
      return false;
  return true;
}

static const char *take_string(const uint8_t *data, size_t length,
                               size_t *cursor, char *out, size_t capacity) {
  if (length - *cursor < 2)
    return "identity section is truncated";
  const size_t n = (size_t)data[*cursor] | (size_t)data[*cursor + 1] << 8;
  *cursor += 2;
  if (n > length - *cursor)
    return "identity section is truncated";
  if (n == 0 || n >= capacity)
    return "identity string length is outside the host limits";
  if (memchr(data + *cursor, 0, n))
    return "identity string contains NUL";
  memcpy(out, data + *cursor, n);
  out[n] = 0;
  *cursor += n;
  return NULL;
}

static const char *identity(const reader *r, const package_profile *profile,
                            package_identity *out) {
  uint8_t header[HEADER_SIZE];
  if (r->size < HEADER_SIZE + FOOTER_SIZE ||
      !r->read(r->context, 0, header, sizeof(header)))
    return "file is shorter than a package header";
  if (u32(header) != PACKAGE_MAGIC)
    return "not a .pocket package";
  if (u32(header + 4) != PACKAGE_VERSION)
    return "unsupported package format version";
  const uint64_t manifest = u32(header + 8);
  const uint32_t variants = u32(header + 12);
  if (!variants || variants > MAX_VARIANTS)
    return "variant count is outside the host limits";
  const uint64_t table = HEADER_SIZE + (manifest + 15) / 16 * 16;
  if (!within(r, table, (uint64_t)variants * VARIANT_SIZE))
    return "variant table exceeds the file";
  for (uint32_t v = 0; v < variants; v++) {
    uint8_t variant[VARIANT_SIZE];
    if (!r->read(r->context, table + (uint64_t)v * VARIANT_SIZE, variant,
                 sizeof(variant)))
      return "variant table read failed";
    if (u32(variant + 8) != profile->host_abi ||
        memcmp(variant + 32, profile->profile_hash, 32))
      continue;
    const uint32_t sections = u32(variant + 12);
    const uint64_t offset = u32(variant + 16);
    if (sections > MAX_SECTIONS ||
        !within(r, offset, (uint64_t)sections * SECTION_SIZE))
      return "section table exceeds the file";
    for (uint32_t s = 0; s < sections; s++) {
      uint8_t section[SECTION_SIZE];
      if (!r->read(r->context, offset + (uint64_t)s * SECTION_SIZE, section,
                   sizeof(section)))
        return "section table read failed";
      if (u32(section) != SECTION_IDENTITY)
        continue;
      const uint64_t start = u32(section + 8), length = u32(section + 12);
      if (length > MAX_IDENTITY || !within(r, start, length))
        return "identity section is outside the host limits";
      uint8_t bytes[MAX_IDENTITY];
      if (!r->read(r->context, start, bytes, (size_t)length))
        return "identity section read failed";
      size_t cursor = 0;
      const char *error;
      if ((error = take_string(bytes, (size_t)length, &cursor, out->output,
                               sizeof(out->output))) ||
          (error = take_string(bytes, (size_t)length, &cursor, out->id,
                               sizeof(out->id))) ||
          (error = take_string(bytes, (size_t)length, &cursor, out->title,
                               sizeof(out->title))))
        return error;
      if (!valid_output(out->output))
        return "app output contains characters outside [A-Za-z0-9._-]";
      out->bytes = r->size;
      return NULL;
    }
    return "matching variant has no identity section";
  }
  return "no variant for this device profile and binding generation";
}

static bool read_file(void *context, uint64_t offset, void *out,
                      size_t length) {
  FILE *file = context;
  return fseek(file, (long)offset, SEEK_SET) == 0 &&
         fread(out, 1, length, file) == length;
}

static bool read_memory(void *context, uint64_t offset, void *out,
                        size_t length) {
  memcpy(out, (const uint8_t *)context + offset, length);
  return true;
}

const char *package_identity_file(FILE *file, uint64_t size,
                                  const package_profile *profile,
                                  package_identity *out) {
  if (size > INT32_MAX)
    return "package exceeds 2 GiB";
  const reader r = {read_file, file, size};
  return identity(&r, profile, out);
}

const char *package_identity_memory(const uint8_t *data, size_t size,
                                    const package_profile *profile,
                                    package_identity *out) {
  const reader r = {read_memory, (void *)data, size};
  return identity(&r, profile, out);
}

const char *package_verify_file(FILE *file, uint64_t size) {
  if (size < HEADER_SIZE + FOOTER_SIZE || size > INT32_MAX ||
      fseek(file, 0, SEEK_SET))
    return "package size is outside the host limits";
  uint64_t hash = 0xcbf29ce484222325ull;
  uint8_t block[4096];
  for (uint64_t left = size - FOOTER_SIZE; left;) {
    const size_t n = left < sizeof(block) ? (size_t)left : sizeof(block);
    if (fread(block, 1, n, file) != n)
      return "package read failed";
    for (size_t i = 0; i < n; i++)
      hash = (hash ^ block[i]) * 0x100000001b3ull;
    left -= n;
  }
  uint8_t footer[FOOTER_SIZE];
  if (fread(footer, 1, sizeof(footer), file) != sizeof(footer))
    return "package footer read failed";
  uint64_t stored = 0;
  for (int i = 7; i >= 0; i--)
    stored = stored << 8 | footer[i];
  return stored == hash ? NULL : "package checksum mismatch";
}

const char *package_worker_memory(const uint8_t *data, size_t size,
                                  const package_profile *profile,
                                  const char **source, size_t *source_size) {
  if (!data || !profile || !profile->profile_hash || !profile->target ||
      !profile->target_length || !source || !source_size ||
      size < HEADER_SIZE + FOOTER_SIZE)
    return "invalid worker package";
  *source = NULL;
  *source_size = 0;
  if (u32(data) != PACKAGE_MAGIC || u32(data + 4) != PACKAGE_VERSION)
    return "unsupported worker package";
  const reader r = {.size = size};
  const uint64_t table = HEADER_SIZE + ((uint64_t)u32(data + 8) + 15) / 16 * 16;
  const uint32_t variants = u32(data + 12);
  if (!variants || variants > MAX_VARIANTS ||
      !within(&r, table, (uint64_t)variants * VARIANT_SIZE))
    return "worker variant table exceeds package";
  for (uint32_t v = 0; v < variants; ++v) {
    const uint8_t *variant = data + table + (uint64_t)v * VARIANT_SIZE;
    if (u32(variant + 8) != profile->host_abi ||
        memcmp(variant + 32, profile->profile_hash, 32))
      continue;
    const uint64_t target_offset = u32(variant),
                   target_length = u32(variant + 4);
    if (!within(&r, target_offset, target_length))
      return "worker variant target exceeds package";
    if (target_length != profile->target_length ||
        memcmp(data + target_offset, profile->target, (size_t)target_length))
      continue;
    const uint32_t count = u32(variant + 12);
    const uint64_t offset = u32(variant + 16);
    if (count > MAX_SECTIONS ||
        !within(&r, offset, (uint64_t)count * SECTION_SIZE))
      return "worker section table exceeds package";
    for (uint32_t s = 0; s < count; ++s) {
      const uint8_t *section = data + offset + (uint64_t)s * SECTION_SIZE;
      if (u32(section) != 8)
        continue;
      const uint64_t start = u32(section + 8), length = u32(section + 12);
      if (*source || length < 2 || length > 1024 * 1024 ||
          !within(&r, start, length) || data[start + length - 1] != 0)
        return "invalid worker source section";
      *source = (const char *)(data + start);
      *source_size = (size_t)length - 1;
    }
    return NULL;
  }
  return "no worker variant for host profile";
}
