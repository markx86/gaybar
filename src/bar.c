#include <bar.h>
#include <wl.h>
#include <log.h>
#include <util.h>
#include <list.h>
#include <assert.h>
#include <compiler.h>

#include <stdlib.h>
#include <time.h>

struct zone_private {
  struct list link;
  b8 redraw;
  u32 offset;
  struct zone zone;
};
#define ZONEPRIVATE(x) CONTAINEROF(x, struct zone_private, zone)

struct bar {
  enum bar_position position;
  u32 thickness;
  u32 sizes[ZONE_POSITION_MAX];
  struct wl_list zones;
};

static struct bar bar = {0};

enum bar_position bar_get_position(void) { return bar.position; }
u32 bar_get_thickness(void) { return bar.thickness; }

static inline void get_time(struct timespec* tm) {
  clock_gettime(CLOCK_MONOTONIC, tm);
}

static void render(void) {
  struct zone_private* zone_private;
  list_for_each(zone_private, &bar.zones, link) {
    if (zone_private->redraw) {
      wl_draw_zone(&zone_private->zone, zone_private->offset,
                   bar.sizes[zone_private->zone.position]);
      zone_private->redraw = false;
    }
  }
}

static int tick(f32 delta) {
  UNUSED(delta);
  return 0;
}

static const char* position_string(enum bar_position position) {
  switch (position) {
    case BAR_POSITION_TOP:
      return "top";
    case BAR_POSITION_BOTTOM:
      return "bottom";
    default:
      log_fatal("invalid bar position %d", position);
  }
}

int bar_init(enum bar_position position, u32 thickness) {
  log_trace("creating bar anchored on the %s of the screen with thickness %u",
            position_string(position), thickness);
  {
    bar.position = position;
    bar.thickness = thickness;
    list_init(&bar.zones);
  }
  return wl_init();
}

int bar_loop(void) {
  int rc;
  struct timespec now, prev;
  f32 delta;

  get_time(&prev);

  while (!wl_should_close()) {
    get_time(&now);
    delta = (f32)(now.tv_sec - prev.tv_sec) +
            (f32)(now.tv_nsec - prev.tv_nsec) / 1e9;
    prev = now;

    rc = tick(delta);
    if (rc < 0)
      goto fail;

    if (wl_draw_begin()) {
      render();
      wl_draw_end();
    }
  }

  rc = 0;
fail:
  return rc;
}

static void destroy_zone_private(struct zone_private* zone_private) {
  list_remove(&zone_private->link);

  free(zone_private->zone.image_buffer);
  free(zone_private);
}

void bar_cleanup(void) {
  struct zone_private *zone_private, *next_zone_private;
  list_for_each_safe(zone_private, next_zone_private, &bar.zones, link)
    destroy_zone_private(zone_private);
  wl_cleanup();
}

struct zone* bar_alloc_zone(enum zone_position position, u32 size) {
  struct zone_private* zone_private;
  struct zone* zone;

  ASSERT(position <= ZONE_POSITION_MAX);

  zone_private = zalloc(sizeof(*zone_private));
  ASSERT(zone_private != NULL);

  {
    zone_private->redraw = false;
    zone_private->offset = bar.sizes[position];
    list_insert(&bar.zones, &zone_private->link);
  }

  bar.sizes[position] += size;

  zone = &zone_private->zone;
  /* NOTE: This works for bars that are horizontal. For vertical bars, the
   *       dimensions should be reversed.
   */
  {
    zone->position = position;
    zone->width = size;
    zone->height = bar.thickness;
  }
  zone->image_buffer =
    zalloc(size * bar.thickness * sizeof(*zone->image_buffer));
  ASSERT(zone->image_buffer != NULL);

  return zone;
}

void bar_destroy_zone(struct zone** zonep) {
  struct zone_private* zone_private;

  ASSERT(zonep != NULL);
  ASSERT(*zonep != NULL);

  zone_private = ZONEPRIVATE(*zonep);
  destroy_zone_private(zone_private);

  *zonep = NULL;
}

void bar_redraw_zone(struct zone* zone) {
  ASSERT(zone != NULL);
  ZONEPRIVATE(zone)->redraw = true;
}

b8 bar_should_redraw_zone(struct zone* zone) {
  ASSERT(zone != NULL);
  return ZONEPRIVATE(zone)->redraw;
}
