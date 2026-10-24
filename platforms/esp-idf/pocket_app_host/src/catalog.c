#include "catalog.h"
#include "esp_log.h"
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "pocket_apps";

#define CATALOG_MAX 128
#define INBOX_MAX 32

static bool has_suffix(const char *name, const char *suffix) {
  const size_t n = strlen(name), s = strlen(suffix);
  return n > s && strcasecmp(name + n - s, suffix) == 0;
}

static void record(catalog *c, const char *file, const char *message) {
  ESP_LOGW(TAG, "%s: %s", file, message);
  if (c->error_count == CATALOG_ERRORS)
    return;
  catalog_error *e = &c->errors[c->error_count++];
  snprintf(e->file, sizeof(e->file), "%s", file);
  snprintf(e->message, sizeof(e->message), "%s", message);
}

static bool ensure_dir(const char *path) {
  char partial[CATALOG_PATH_MAX];
  const size_t length = strlen(path);
  if (!length || length >= sizeof(partial))
    return false;
  memcpy(partial, path, length + 1);
  /* The first component is the VFS mount point; FAT cannot stat its root. */
  for (char *c = strchr(partial + 1, '/'); c;) {
    char *next = strchr(c + 1, '/');
    if (next)
      *next = 0;
    if (mkdir(partial, 0775) != 0 && errno != EEXIST)
      return false;
    if (next)
      *next = '/';
    c = next;
  }
  return true;
}

static const char *inspect(const char *path, const package_profile *profile,
                           bool verify, package_identity *out) {
  FILE *file = fopen(path, "rb");
  if (!file)
    return "open failed";
  struct stat st;
  const char *error = fstat(fileno(file), &st) != 0 ? "stat failed" : NULL;
  if (!error)
    error = package_identity_file(file, (uint64_t)st.st_size, profile, out);
  if (!error && verify)
    error = package_verify_file(file, (uint64_t)st.st_size);
  fclose(file);
  return error;
}

/* Names are collected first: renaming entries while readdir walks a FAT
 * directory can revisit or skip them. */
static size_t list_packages(const char *directory,
                            char (*names)[CATALOG_PATH_MAX], size_t capacity) {
  DIR *dir = opendir(directory);
  if (!dir)
    return 0;
  size_t count = 0;
  /* Hidden names include the AppleDouble "._<file>" metadata macOS writes
   * next to every file it copies onto FAT. */
  for (struct dirent *entry; count < capacity && (entry = readdir(dir));)
    if (entry->d_type != DT_DIR && entry->d_name[0] != '.' &&
        has_suffix(entry->d_name, ".pocket"))
      snprintf(names[count++], CATALOG_PATH_MAX, "%s", entry->d_name);
  closedir(dir);
  return count;
}

void catalog_install(catalog *c, const char *inbox, const char *apps,
                     const package_profile *profile, const char *reserved) {
  if (!ensure_dir(inbox) || !ensure_dir(apps)) {
    record(c, inbox, "cannot create the package directories");
    return;
  }
  char (*names)[CATALOG_PATH_MAX] = calloc(INBOX_MAX, CATALOG_PATH_MAX);
  if (!names)
    return;
  const size_t count = list_packages(inbox, names, INBOX_MAX);
  for (size_t i = 0; i < count; i++) {
    char source[CATALOG_PATH_MAX], target[CATALOG_PATH_MAX];
    package_identity identity;
    const char *error = NULL;
    if ((size_t)snprintf(source, sizeof(source), "%s/%s", inbox, names[i]) >=
        sizeof(source))
      error = "path exceeds the host limit";
    if (!error)
      error = inspect(source, profile, true, &identity);
    if (!error && reserved && !strcmp(identity.output, reserved))
      error = "output is reserved for the home app";
    if (!error && (size_t)snprintf(target, sizeof(target), "%s/%s.pocket", apps,
                                   identity.output) >= sizeof(target))
      error = "path exceeds the host limit";
    if (!error) {
      unlink(target);
      if (rename(source, target) != 0)
        error = "move into the apps directory failed";
      else
        ESP_LOGI(TAG, "installed %s (%s) from %s", identity.output, identity.id,
                 names[i]);
    }
    if (error) {
      record(c, names[i], error);
      char rejected[CATALOG_PATH_MAX + 16];
      snprintf(rejected, sizeof(rejected), "%s.rejected", source);
      unlink(rejected);
      rename(source, rejected);
    }
  }
  free(names);
}

static int by_title(const void *a, const void *b) {
  const catalog_app *x = a, *y = b;
  const int order = strcasecmp(x->identity.title, y->identity.title);
  return order ? order : strcmp(x->identity.output, y->identity.output);
}

void catalog_scan(catalog *c, const char *apps, const package_profile *profile,
                  const char *reserved) {
  struct stat st;
  c->storage = stat(apps, &st) == 0 && S_ISDIR(st.st_mode);
  if (!c->storage)
    return;
  char (*names)[CATALOG_PATH_MAX] = calloc(CATALOG_MAX, CATALOG_PATH_MAX);
  if (!names)
    return;
  const size_t count = list_packages(apps, names, CATALOG_MAX);
  for (size_t i = 0; i < count; i++) {
    catalog_app app = {0};
    if ((size_t)snprintf(app.path, sizeof(app.path), "%s/%s", apps, names[i]) >=
        sizeof(app.path)) {
      record(c, names[i], "path exceeds the host limit");
      continue;
    }
    const char *error = inspect(app.path, profile, false, &app.identity);
    if (!error && reserved && !strcmp(app.identity.output, reserved))
      error = "output is reserved for the home app";
    if (!error && catalog_find(c, app.identity.output))
      error = "another package has the same output";
    if (error) {
      record(c, names[i], error);
      continue;
    }
    if (c->count == c->capacity) {
      const size_t capacity = c->capacity ? c->capacity * 2 : 16;
      catalog_app *grown = realloc(c->apps, capacity * sizeof(*grown));
      if (!grown)
        break;
      c->apps = grown;
      c->capacity = capacity;
    }
    c->apps[c->count++] = app;
  }
  free(names);
  if (c->count)
    qsort(c->apps, c->count, sizeof(*c->apps), by_title);
}

const catalog_app *catalog_find(const catalog *c, const char *output) {
  for (size_t i = 0; i < c->count; i++)
    if (!strcmp(c->apps[i].identity.output, output))
      return &c->apps[i];
  return NULL;
}

void catalog_free(catalog *c) {
  free(c->apps);
  *c = (catalog){0};
}
