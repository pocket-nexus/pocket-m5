#pragma once
#include "package_info.h"
#include <stdbool.h>
#include <stddef.h>

#define CATALOG_PATH_MAX 192
#define CATALOG_ERRORS 8

typedef struct {
  package_identity identity;
  char path[CATALOG_PATH_MAX];
} catalog_app;

typedef struct {
  char file[64];
  char message[96];
} catalog_error;

typedef struct {
  catalog_app *apps;
  size_t count, capacity;
  catalog_error errors[CATALOG_ERRORS];
  size_t error_count;
  bool storage;
} catalog;

/* Validates every *.pocket in inbox and moves it to apps/<output>.pocket,
 * replacing an installed package with the same output. A rejected file is
 * renamed to <file>.rejected. Creates both directories when absent. reserved
 * is an output no installed package may use (the home app). */
void catalog_install(catalog *c, const char *inbox, const char *apps,
                     const package_profile *profile, const char *reserved);
/* Lists *.pocket in apps whose matching variant has an identity, sorted by
 * title, skipping the reserved output. */
void catalog_scan(catalog *c, const char *apps, const package_profile *profile,
                  const char *reserved);
const catalog_app *catalog_find(const catalog *c, const char *output);
void catalog_free(catalog *c);
