/* programs/m5stack-tab5/components/pocket_system/src/companion.c with the
 * Pocket Link client it drives (engine/host-services link.c and link_http.c
 * under POCKETJS_WORKER_HTTP), on a POSIX host_services.h, against the
 * in-process companion of tests/companion-route.test.ts:
 *
 *   companion-route <store> <pair-uri> <slow-url>
 *
 * Each step prints one line; `wait <name>` lines ask the test to act and
 * the harness reads one line from stdin before it goes on:
 *
 *   start <url> error <message>          the transport refused the request
 *   fetch <url> <status> <end> <body>    headers status, end status, body
 *   end <url> <status> <message>         a request the link ended */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "companion.h"
#include "host_services.h"
#include "jscan.h"
#include "launcher.h"
#include "link.h"

static char apps[512];
static char link_directory[512];

uint64_t services_host_us(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
}

uint64_t services_host_ms(void) { return services_host_us() / 1000u; }

uint64_t services_host_epoch_ms(void) {
  struct timeval now;
  gettimeofday(&now, NULL);
  return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_usec / 1000u;
}

void services_host_random(uint8_t *out, size_t length) {
  for (size_t i = 0; i < length; i += 1)
    out[i] = (uint8_t)rand();
}

const char *launcher_host_apps_directory(void) { return apps; }

bool launcher_host_slot_path(const char *slot, const char *name, char *out,
                             size_t capacity) {
  int written = name == NULL
                    ? snprintf(out, capacity, "%s/%s", apps, slot)
                    : snprintf(out, capacity, "%s/%s/%s", apps, slot, name);
  return written > 0 && (size_t)written < capacity;
}

bool launcher_host_active_hash(const char *slot, uint64_t *hash) {
  (void)slot;
  (void)hash;
  return false;
}

bool launcher_host_load(const char *slot, uint64_t hash, LauncherPackage *out,
                        void **held) {
  (void)slot;
  (void)hash;
  (void)out;
  (void)held;
  return false;
}

void services_host_release(void *held) { (void)held; }

const char *link_host_target(void) { return "tab5-home"; }

void link_host_identity(char *name, size_t name_capacity, char *model,
                        size_t model_capacity) {
  snprintf(name, name_capacity, "Harness");
  snprintf(model, model_capacity, "M5Stack Tab5");
}

const char *link_host_directory(void) { return link_directory; }

bool link_host_net_ready(void) { return true; }

void link_host_address(char *out, size_t capacity) {
  snprintf(out, capacity, "127.0.0.1");
}

bool link_host_staging_path(const char *slot, char *out, size_t capacity) {
  (void)slot;
  (void)out;
  (void)capacity;
  return false;
}

bool link_host_install(const char *slot, const char *path, uint64_t hash,
                       LauncherPackage *out, void **held, char *error,
                       size_t error_capacity) {
  (void)slot;
  (void)path;
  (void)hash;
  (void)out;
  (void)held;
  snprintf(error, error_capacity, "no installs here");
  return false;
}

bool link_host_remove(const char *slot) {
  (void)slot;
  return false;
}

void link_host_linked(bool up) { printf("linked %d\n", up); }

bool link_host_ctrl(const char *json, size_t length) {
  (void)json;
  (void)length;
  return false;
}

bool link_host_data(uint32_t stream, const uint8_t *bytes, size_t length) {
  (void)stream;
  (void)bytes;
  (void)length;
  return false;
}

static void await_test(const char *name) {
  char line[64];
  printf("wait %s\n", name);
  fflush(stdout);
  if (fgets(line, sizeof line, stdin) == NULL)
    exit(20);
}

/* Pump until the link's state is (or, with `is` false, is not) `state`. */
static bool await_state(const char *state, bool is) {
  for (int i = 0; i < 5000; i += 1) {
    link_pump();
    const char *status = link_status_json();
    char now[16] = "";
    json_find_string(json_span(status, strlen(status)), "state", now,
                     sizeof now);
    if ((strcmp(now, state) == 0) == is)
      return true;
    usleep(2000);
  }
  return false;
}

static uint32_t start(const char *url) {
  char descriptor[512], error[160] = "";
  snprintf(descriptor, sizeof descriptor, "{\"url\":\"%s\",\"timeoutMs\":5000}",
           url);
  const uint32_t id = companion_start((const uint8_t *)descriptor,
                                      strlen(descriptor), error, sizeof error);
  if (!id)
    printf("start %s error %s\n", url, error);
  return id;
}

/* Pump until the request ends; print its facts in one line. */
static void fetch(const char *url) {
  const uint32_t id = start(url);
  if (!id)
    return;
  static pocket_http_esp_fact fact;
  char head[1100] = "", body[2048] = "";
  size_t body_length = 0;
  for (int i = 0; i < 5000; i += 1) {
    link_pump();
    while (companion_take(id, &fact)) {
      if (fact.kind == POCKET_WORKER_HEADERS) {
        memcpy(head, fact.bytes, fact.length);
        head[fact.length] = 0;
      } else if (fact.kind == POCKET_WORKER_DATA) {
        const size_t take = fact.length < sizeof body - 1 - body_length
                                ? fact.length
                                : sizeof body - 1 - body_length;
        memcpy(body + body_length, fact.bytes, take);
        body_length += take;
      } else {
        uint64_t status = 0;
        json_find_u64(json_span(head, strlen(head)), "status", &status);
        body[body_length] = 0;
        printf("fetch %s %u %d %s\n", url, (unsigned)status, (int)fact.status,
               body);
        return;
      }
    }
    usleep(1000);
  }
  printf("fetch %s no end\n", url);
}

int main(int argc, char **argv) {
  if (argc != 4)
    return 2;
  setvbuf(stdout, NULL, _IOLBF, 0);
  mkdir(argv[1], 0777);
  snprintf(apps, sizeof apps, "%s/apps", argv[1]);
  snprintf(link_directory, sizeof link_directory, "%s/link", argv[1]);
  static const char launcher_plan[] = "{\"app\":{\"output\":\"pocket-shell\","
                                      "\"id\":\"dev.pocket-nexus.shell\"}}";
  static const char hn_plan[] =
      "{\"app\":{\"output\":\"hn\",\"id\":\"dev.pocket-nexus.hn\"}}";
  const LauncherPackage launcher = {.plan = launcher_plan,
                                    .plan_length = sizeof launcher_plan - 1};
  const LauncherPackage hn = {.plan = hn_plan,
                              .plan_length = sizeof hn_plan - 1};
  launcher_init("0123456789abcdef", &launcher);
  launcher_set_current("fedcba9876543210", &hn);
  link_init();

  /* No companion paired yet. */
  start("https://companion.pocket/x/status");

  if (!link_pair(argv[2], strlen(argv[2])))
    return 3;
  printf("status %s\n", await_state("linked", true) ? "linked" : "not linked");

  /* The companion answers companion.pocket itself, for the running app. */
  fetch("https://companion.pocket/nothing/here");
  fetch("https://companion.pocket/x/status");
  launcher_set_current("0123456789abcdef", &launcher);
  fetch("https://companion.pocket/x/status");

  /* A request the link closes under it, then no link at all. */
  const uint32_t slow = start(argv[3]);
  if (!slow)
    return 4;
  for (int i = 0; i < 50; i += 1) {
    link_pump();
    usleep(2000);
  }
  await_test("stop");
  static pocket_http_esp_fact fact;
  bool ended = false;
  for (int i = 0; i < 5000 && !ended; i += 1) {
    link_pump();
    while (!ended && companion_take(slow, &fact))
      if (fact.kind == POCKET_WORKER_END) {
        printf("end %s %d %.*s\n", argv[3], (int)fact.status, (int)fact.length,
               (const char *)fact.bytes);
        ended = true;
      }
    usleep(1000);
  }
  if (!ended)
    printf("end %s none\n", argv[3]);
  printf("status %s\n",
         await_state("linked", false) ? "not linked" : "still linked");
  start("https://companion.pocket/x/status");
  link_shutdown();
  return 0;
}
