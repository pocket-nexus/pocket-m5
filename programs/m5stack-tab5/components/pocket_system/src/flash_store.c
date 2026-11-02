/*
 * Installed packages on a raw flash partition, read in place through one
 * read-only mapping of the whole partition.
 *
 * The partition is a run of 64 KiB pages (CONFIG_MMU_PAGE_SIZE). A stored
 * package is an extent of whole pages: a 64-byte header, the package bytes
 * from offset 64 (16-byte aligned, as an embedded package is) and padding to
 * the page boundary. The bytes are written before the header, so a header
 * whose magic, extent and hash agree with the bytes behind it is a committed
 * package, and the pages of an interrupted install, which carry no such
 * header, are free. Removal clears the header's state word in place: flash
 * clears bits without an erase. When two committed extents name one slot,
 * the higher sequence number is the package and the other is cleared.
 *
 * Every function runs on the system task (system_run): a write disables the
 * cache, and the entry table is read and changed on that task alone.
 */

#include "system_internal.h"

#include <stddef.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "sdkconfig.h"

#ifndef CONFIG_MMU_PAGE_SIZE
#define CONFIG_MMU_PAGE_SIZE 0x10000
#endif
#define PAGE_BYTES CONFIG_MMU_PAGE_SIZE
#define HEADER_BYTES 64
#define HEADER_MAGIC 0x50414b50u /* "PKAP" */
#define HEADER_VERSION 1u
#define STATE_LIVE 0xffffffffu
#define WRITE_CHUNK 8192
/* Header, footer and the smallest package that parses. */
#define MIN_PACKAGE_BYTES 40

static const char *TAG = "pocket_system";

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t pages;
  uint32_t sequence;
  uint32_t reserved;
  uint64_t hash;
  char slot[16];
  uint8_t padding[HEADER_BYTES - 52];
  uint32_t state;
} flash_header_t;
_Static_assert(sizeof(flash_header_t) == HEADER_BYTES, "header layout");
_Static_assert(offsetof(flash_header_t, state) == HEADER_BYTES - 4,
               "the state word ends the header");

typedef struct {
  uint32_t page;
  uint32_t pages;
  uint32_t size;
  uint32_t sequence;
  uint64_t hash;
  char slot[17];
} flash_entry_t;

static const esp_partition_t *partition;
static const uint8_t *mapped;
static esp_partition_mmap_handle_t mapping;
static uint32_t page_count;
static flash_entry_t entries[FLASH_STORE_MAX_ENTRIES];
static size_t entry_count;
static uint32_t next_sequence = 1;

static const flash_header_t *header_at(uint32_t page) {
  return (const flash_header_t *)(mapped + (size_t)page * PAGE_BYTES);
}

static const uint8_t *bytes_at(uint32_t page) {
  return mapped + (size_t)page * PAGE_BYTES + HEADER_BYTES;
}

static uint64_t footer_of(const uint8_t *bytes, size_t size) {
  uint64_t hash = 0;
  for (int i = 7; i >= 0; i -= 1)
    hash = (hash << 8) | bytes[size - 8 + (size_t)i];
  return hash;
}

static bool hex_slot(const char *slot) {
  for (size_t i = 0; i < 16; ++i)
    if (!((slot[i] >= '0' && slot[i] <= '9') ||
          (slot[i] >= 'a' && slot[i] <= 'f')))
      return false;
  return true;
}

/* A committed extent at `page`: the header's fields fit the partition and
 * the footer behind the bytes is the hash it names. */
static bool extent_valid(uint32_t page, const flash_header_t *h) {
  if (h->magic != HEADER_MAGIC || h->version != HEADER_VERSION ||
      h->pages == 0 || h->pages > page_count - page ||
      h->size < MIN_PACKAGE_BYTES ||
      (uint64_t)h->size + HEADER_BYTES > (uint64_t)h->pages * PAGE_BYTES ||
      !hex_slot(h->slot))
    return false;
  return footer_of(bytes_at(page), h->size) == h->hash;
}

static flash_entry_t *entry_for(const char *slot) {
  if (slot == NULL)
    return NULL;
  for (size_t i = 0; i < entry_count; ++i)
    if (strcmp(entries[i].slot, slot) == 0)
      return &entries[i];
  return NULL;
}

static void drop_entry(flash_entry_t *entry) {
  const size_t index = (size_t)(entry - entries);
  memmove(entry, entry + 1, (entry_count - index - 1) * sizeof *entry);
  entry_count -= 1;
}

/* Clear the state word of the extent at `page`: the package is gone and
 * its pages are free. */
static esp_err_t clear_extent(uint32_t page) {
  const uint32_t cleared = 0;
  return esp_partition_write(
      partition, (size_t)page * PAGE_BYTES + offsetof(flash_header_t, state),
      &cleared, sizeof cleared);
}

/* The first run of `pages` free pages. */
static bool first_fit(uint32_t pages, uint32_t *out) {
  uint32_t start = 0;
  while (pages <= page_count && start <= page_count - pages) {
    uint32_t next = start;
    for (size_t i = 0; i < entry_count; ++i) {
      const flash_entry_t *e = &entries[i];
      if (e->page < start + pages && start < e->page + e->pages &&
          e->page + e->pages > next)
        next = e->page + e->pages;
    }
    if (next == start) {
      *out = start;
      return true;
    }
    start = next;
  }
  return false;
}

static uint32_t used_pages(void) {
  uint32_t used = 0;
  for (size_t i = 0; i < entry_count; ++i)
    used += entries[i].pages;
  return used;
}

esp_err_t flash_store_open(const char *label) {
  if (partition != NULL)
    return ESP_ERR_INVALID_STATE;
  if (label == NULL)
    return ESP_ERR_INVALID_ARG;
  const esp_partition_t *found = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
  if (found == NULL)
    return ESP_ERR_NOT_FOUND;
  if (found->size < PAGE_BYTES || found->size % PAGE_BYTES != 0 ||
      found->address % PAGE_BYTES != 0)
    return ESP_ERR_INVALID_SIZE;
  const void *pointer = NULL;
  esp_partition_mmap_handle_t handle = 0;
  const esp_err_t result = esp_partition_mmap(
      found, 0, found->size, ESP_PARTITION_MMAP_DATA, &pointer, &handle);
  if (result != ESP_OK)
    return result;
  partition = found;
  mapped = pointer;
  mapping = handle;
  page_count = found->size / PAGE_BYTES;
  entry_count = 0;
  next_sequence = 1;
  for (uint32_t page = 0; page < page_count;) {
    const flash_header_t *h = header_at(page);
    if (!extent_valid(page, h)) {
      page += 1;
      continue;
    }
    if (h->state == STATE_LIVE) {
      if (h->sequence >= next_sequence)
        next_sequence = h->sequence + 1;
      flash_entry_t entry = {.page = page,
                             .pages = h->pages,
                             .size = h->size,
                             .sequence = h->sequence,
                             .hash = h->hash};
      memcpy(entry.slot, h->slot, 16);
      flash_entry_t *earlier = entry_for(entry.slot);
      if (earlier != NULL) {
        /* An install replaced the slot and ended before it cleared the
         * earlier package. */
        if (earlier->sequence < entry.sequence) {
          clear_extent(earlier->page);
          *earlier = entry;
        } else {
          clear_extent(page);
        }
      } else if (entry_count < FLASH_STORE_MAX_ENTRIES) {
        entries[entry_count++] = entry;
      } else {
        ESP_LOGW(TAG, "apps: more than %d packages; slot %s is left out",
                 FLASH_STORE_MAX_ENTRIES, entry.slot);
      }
    }
    page += h->pages;
  }
  ESP_LOGI(TAG, "apps: %u packages on %s, %u of %u pages free",
           (unsigned)entry_count, label, (unsigned)(page_count - used_pages()),
           (unsigned)page_count);
  return ESP_OK;
}

bool flash_store_ready(void) { return partition != NULL; }

size_t flash_store_free_bytes(void) {
  if (partition == NULL)
    return 0;
  return (size_t)(page_count - used_pages()) * PAGE_BYTES;
}

const uint8_t *flash_store_find(const char *slot, size_t *size,
                                uint64_t *hash) {
  const flash_entry_t *entry = partition == NULL ? NULL : entry_for(slot);
  if (entry == NULL)
    return NULL;
  if (size != NULL)
    *size = entry->size;
  if (hash != NULL)
    *hash = entry->hash;
  return bytes_at(entry->page);
}

size_t flash_store_count(void) { return entry_count; }

const uint8_t *flash_store_at(size_t index, char slot[17], size_t *size,
                              uint64_t *hash) {
  if (index >= entry_count)
    return NULL;
  const flash_entry_t *entry = &entries[index];
  memcpy(slot, entry->slot, 17);
  if (size != NULL)
    *size = entry->size;
  if (hash != NULL)
    *hash = entry->hash;
  return bytes_at(entry->page);
}

esp_err_t flash_store_install(const char *slot, const uint8_t *bytes,
                              size_t size, uint64_t hash,
                              const uint8_t **stored) {
  if (partition == NULL)
    return ESP_ERR_INVALID_STATE;
  if (slot == NULL || strlen(slot) != 16 || !hex_slot(slot) || bytes == NULL ||
      size < MIN_PACKAGE_BYTES || size > UINT32_MAX - HEADER_BYTES)
    return ESP_ERR_INVALID_ARG;
  flash_entry_t *earlier = entry_for(slot);
  if (earlier == NULL && entry_count == FLASH_STORE_MAX_ENTRIES)
    return ESP_ERR_NO_MEM;
  const uint32_t pages =
      (uint32_t)((size + HEADER_BYTES + PAGE_BYTES - 1) / PAGE_BYTES);
  uint32_t page = 0;
  if (!first_fit(pages, &page)) {
    /* Reuse the slot's own pages: an interrupted install loses the app, as
     * the card's rename did, never leaves a package the firmware would
     * refuse. */
    if (earlier == NULL)
      return ESP_ERR_NO_MEM;
    const esp_err_t cleared = clear_extent(earlier->page);
    if (cleared != ESP_OK)
      return cleared;
    drop_entry(earlier);
    earlier = NULL;
    if (!first_fit(pages, &page))
      return ESP_ERR_NO_MEM;
  }
  /* The source may be in PSRAM, which a flash write cannot read: the driver
   * would fall back to 32-byte programs. Stage each chunk in internal RAM. */
  uint8_t *chunk =
      heap_caps_malloc(WRITE_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (chunk == NULL)
    return ESP_ERR_NO_MEM;
  const size_t base = (size_t)page * PAGE_BYTES;
  esp_err_t result =
      esp_partition_erase_range(partition, base, (size_t)pages * PAGE_BYTES);
  for (size_t at = 0; result == ESP_OK && at < size; at += WRITE_CHUNK) {
    const size_t count = size - at < WRITE_CHUNK ? size - at : WRITE_CHUNK;
    memcpy(chunk, bytes + at, count);
    result =
        esp_partition_write(partition, base + HEADER_BYTES + at, chunk, count);
  }
  if (result == ESP_OK) {
    flash_header_t header = {.magic = HEADER_MAGIC,
                             .version = HEADER_VERSION,
                             .size = (uint32_t)size,
                             .pages = pages,
                             .sequence = next_sequence,
                             .hash = hash,
                             .state = STATE_LIVE};
    memcpy(header.slot, slot, 16);
    memset(header.padding, 0xff, sizeof header.padding);
    memcpy(chunk, &header, sizeof header);
    result = esp_partition_write(partition, base, chunk, HEADER_BYTES);
  }
  heap_caps_free(chunk);
  if (result != ESP_OK)
    return result;
  /* The driver flushed the cache: read the extent back through the mapping. */
  if (!extent_valid(page, header_at(page)) ||
      memcmp(bytes_at(page), bytes, size) != 0) {
    clear_extent(page);
    return ESP_ERR_INVALID_CRC;
  }
  if (earlier != NULL) {
    clear_extent(earlier->page);
    drop_entry(earlier);
  }
  flash_entry_t entry = {.page = page,
                         .pages = pages,
                         .size = (uint32_t)size,
                         .sequence = next_sequence,
                         .hash = hash};
  memcpy(entry.slot, slot, 17);
  entries[entry_count++] = entry;
  next_sequence += 1;
  if (stored != NULL)
    *stored = bytes_at(page);
  return ESP_OK;
}

bool flash_store_remove(const char *slot) {
  flash_entry_t *entry = partition == NULL ? NULL : entry_for(slot);
  if (entry == NULL)
    return false;
  const esp_err_t result = clear_extent(entry->page);
  drop_entry(entry);
  return result == ESP_OK;
}
