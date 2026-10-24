#include "pocket/input_esp_touch.h"
#include "sdkconfig.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#define MAX_POINTS CONFIG_ESP_LCD_TOUCH_MAX_POINTS

typedef struct {
  uint32_t id;
  float x, y;
} tracked_contact;

struct pocket_input_esp_touch {
  pocket_input_esp_touch_config config;
  tracked_contact active[MAX_POINTS];
  size_t active_count;
  uint32_t next_id;
  esp_err_t error;
};

pocket_input_esp_touch *
pocket_input_esp_touch_create(const pocket_input_esp_touch_config *c) {
  if (!c || c->struct_size < sizeof(*c) || !c->touch || c->surface > 1 ||
      !isfinite(c->scale_x) || !isfinite(c->scale_y) || c->scale_x <= 0 ||
      c->scale_y <= 0 || c->scale_x > 65535 || c->scale_y > 65535 ||
      !isfinite(c->match_radius) || c->match_radius <= 0)
    return NULL;
  pocket_input_esp_touch *d = calloc(1, sizeof(*d));
  if (d)
    d->config = *c;
  return d;
}

static uint32_t classes(void *context) {
  (void)context;
  return POCKET_INPUT_CLASS_CONTACT;
}

static int32_t cancel_all(pocket_input_esp_touch *d,
                          pocket_input_frame_builder *builder) {
  pocket_cancelled_contact cancelled[MAX_POINTS];
  for (size_t i = 0; i < d->active_count; i++)
    cancelled[i] =
        (pocket_cancelled_contact){.struct_size = sizeof(cancelled[i]),
                                   .surface = d->config.surface,
                                   .id = d->active[i].id};
  const pocket_input_frame frame = {.struct_size = sizeof(frame),
                                    .cancelled =
                                        d->active_count ? cancelled : NULL,
                                    .cancelled_count = d->active_count};
  int32_t result = builder->append(builder->context, &frame);
  if (result == POCKET_STATUS_OK)
    d->active_count = 0;
  return result;
}

/* Greedy nearest match: each sample takes the closest unclaimed contact from
 * the previous poll within match_radius, otherwise a fresh id. */
static uint32_t match(pocket_input_esp_touch *d, bool *claimed, float x,
                      float y) {
  size_t best = MAX_POINTS;
  float best_distance = d->config.match_radius * d->config.match_radius;
  for (size_t i = 0; i < d->active_count; i++) {
    if (claimed[i])
      continue;
    const float dx = d->active[i].x - x, dy = d->active[i].y - y;
    const float distance = dx * dx + dy * dy;
    if (distance <= best_distance) {
      best = i;
      best_distance = distance;
    }
  }
  if (best == MAX_POINTS)
    return d->next_id++;
  claimed[best] = true;
  return d->active[best].id;
}

static int32_t poll(void *context, pocket_input_frame_builder *builder) {
  pocket_input_esp_touch *d = context;
  if (!d || !builder || builder->struct_size < sizeof(*builder) ||
      !builder->append)
    return POCKET_STATUS_INVALID;
  esp_lcd_touch_point_data_t points[MAX_POINTS];
  uint8_t count = 0;
  d->error = esp_lcd_touch_read_data(d->config.touch);
  if (d->error == ESP_OK)
    d->error =
        esp_lcd_touch_get_data(d->config.touch, points, &count, MAX_POINTS);
  if (d->error != ESP_OK)
    return cancel_all(d, builder);
  if (count > MAX_POINTS)
    count = MAX_POINTS;
  bool claimed[MAX_POINTS] = {0};
  tracked_contact next[MAX_POINTS];
  pocket_contact contacts[MAX_POINTS];
  for (uint8_t i = 0; i < count; i++) {
    const float x = (float)points[i].x * d->config.scale_x;
    const float y = (float)points[i].y * d->config.scale_y;
    next[i] = (tracked_contact){.id = match(d, claimed, x, y), .x = x, .y = y};
    contacts[i] = (pocket_contact){.struct_size = sizeof(contacts[i]),
                                   .surface = d->config.surface,
                                   .id = next[i].id,
                                   .x = x,
                                   .y = y};
  }
  const pocket_input_frame frame = {.struct_size = sizeof(frame),
                                    .contacts = count ? contacts : NULL,
                                    .contact_count = count};
  int32_t result = builder->append(builder->context, &frame);
  if (result == POCKET_STATUS_OK) {
    for (uint8_t i = 0; i < count; i++)
      d->active[i] = next[i];
    d->active_count = count;
  }
  return result;
}

static const pocket_input_source_v1 VTABLE = {
    .struct_size = sizeof(VTABLE), .classes = classes, .poll = poll};

const pocket_input_source_v1 *pocket_input_esp_touch_vtable(void) {
  return &VTABLE;
}

esp_err_t pocket_input_esp_touch_last_error(const pocket_input_esp_touch *d) {
  return d ? d->error : ESP_ERR_INVALID_ARG;
}

void pocket_input_esp_touch_destroy(pocket_input_esp_touch *d) { free(d); }
