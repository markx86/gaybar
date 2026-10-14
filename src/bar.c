#include <gaybar/bar.h>
#include <gaybar/wl.h>
#include <gaybar/log.h>
#include <gaybar/util.h>
#include <gaybar/list.h>
#include <gaybar/assert.h>
#include <gaybar/compiler.h>
#include <gaybar/module.h>
#include <gaybar/sched.h>
#include <gaybar/font.h>
#include <gaybar/config.h>

#include <stdlib.h>
#include <string.h>

#define BAR_DEFAULT_POSITION         "top"
#define BAR_DEFAULT_THICKNESS        24
#define BAR_DEFAULT_COLOR_BACKGROUND "#1d1d1d"
#define BAR_DEFAULT_COLOR_FOREGROUND "#eeeeee"

struct zone_private {
  struct list link;
  b8 redraw;
  u32 offset;
  struct zone zone;
};
#define ZONE_PRIVATE(x) CONTAINER_OF(x, struct zone_private, zone)

struct bar {
  enum bar_position position;
  u32 thickness;
  struct color background_color;
  struct color foreground_color;
  u32 sizes[ZONE_POSITION_MAX];
  struct wl_list zones;
};

struct widget {
  struct list link;
  struct module_instance* instance;
};

static struct list g_widgets;
static struct bar g_bar = {0};

u32 bar_get_thickness(void) { return g_bar.thickness; }
enum bar_position bar_get_position(void) { return g_bar.position; }
struct color bar_get_background_color() { return g_bar.background_color; }
struct color bar_get_foreground_color() { return g_bar.foreground_color; }

static void render(void) {
  struct zone_private* zone_private;
  list_for_each(zone_private, &g_bar.zones, link) {
    if (zone_private->redraw) {
      wl_draw_zone(&zone_private->zone,
                   g_bar.sizes[zone_private->zone.position],
                   zone_private->offset);
      zone_private->redraw = false;
    }
  }
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

static enum bar_position position_from_string(const char* s) {
  if (strcmp(s, "bottom") == 0)
    return BAR_POSITION_BOTTOM;
  else if (strcmp(s, "top") == 0)
    return BAR_POSITION_TOP;
  else {
    log_error("invalid position '%s' (can be either 'top' or 'bottom')", s);
    return position_from_string(BAR_DEFAULT_POSITION);
  }
}

static void init_widget(char* widget_name, enum zone_position position) {
  struct module* module;
  struct module_instance* instance;
  struct widget* widget;

  ASSERT(widget_name != NULL);

  module = module_find_by_name(widget_name);
  if (module == NULL) {
    log_error("no module widget '%s' found", widget_name);
    goto out;
  }

  config_with_root_context() {
    config_parse_object_with_key(widget_name) {
      instance = module_init(module, position);
      if (instance == NULL) {
        log_error("could not initialize module '%s'",
                  widget_name);
        break;
      }

      widget = zalloc(sizeof(*widget));
      ASSERT(widget != NULL);

      widget->instance = instance;
      list_insert(&g_widgets, &widget->link);
    }
  }

out:
  free(widget_name);
}

static void init_widgets_from_zone(enum zone_position pos) {
  size_t i;
  const char* pos_key = pos == ZONE_POSITION_LEFT   ? "left" :
                        pos == ZONE_POSITION_CENTER ? "center" :
                        pos == ZONE_POSITION_RIGHT  ? "right" :
                        NULL;

  ASSERT(pos_key != NULL);

  config_parse_array_with_key(pos_key) {
    for (i = 0; i < config_get_array_length(); ++i)
      init_widget(config_get_string_by_idx(i, NULL), pos);
  }
}

static void init_widgets(void) {
  list_init(&g_widgets);

  config_parse_object_with_key("widgets") {
    init_widgets_from_zone(ZONE_POSITION_LEFT);
    init_widgets_from_zone(ZONE_POSITION_CENTER);
    init_widgets_from_zone(ZONE_POSITION_RIGHT);
  }
}

static void get_colors(struct color* background_color,
                       struct color* foreground_color) {
  ASSERT(color_from_hex(BAR_DEFAULT_COLOR_BACKGROUND, background_color));
  ASSERT(color_from_hex(BAR_DEFAULT_COLOR_FOREGROUND, foreground_color));

  config_parse_object_with_key("colors") {
    *background_color =
      config_get_color_by_key("background", *background_color);
    *foreground_color =
      config_get_color_by_key("foreground", *foreground_color);
  }
}

int bar_init(void) {
  int rc;
  enum bar_position position;
  char* position_value;
  i64 thickness;
  struct color background_color, foreground_color;

  position_value = config_get_string_by_key("position", BAR_DEFAULT_POSITION);
  thickness = config_get_int_by_key("thickness", BAR_DEFAULT_THICKNESS);

  position = position_from_string(position_value);
  free(position_value);

  if (thickness < 0) {
    log_error("thickness must be positive (got %ld)", thickness);
    thickness = BAR_DEFAULT_THICKNESS;
  }

  get_colors(&background_color, &foreground_color);

  log_trace("creating bar anchored on the %s of the screen with:",
            position_string(position));
  log_trace(" - thickness = %u", thickness);
  log_trace(" - background color = #%02hhx%02hhx%02hhx",
            background_color.r, background_color.g, background_color.b);
  log_trace(" - foreground color = #%02hhx%02hhx%02hhx",
            foreground_color.r, foreground_color.g, foreground_color.b);

  {
    g_bar.position = position;
    g_bar.thickness = thickness;
    g_bar.background_color = background_color;
    g_bar.foreground_color = foreground_color;
    list_init(&g_bar.zones);
  }

  rc = font_init();
  if (rc < 0)
    goto out;

  rc = wl_init();
  if (rc < 0)
    goto out;

  sched_init();

  init_widgets();

  /* Clear the bar */
  {
    while (!wl_draw_begin())
      ;
    wl_clear(g_bar.background_color.as_u32);
    wl_draw_end();
  }

  rc = 0;
out:
  return rc;
}

void bar_loop(void) {
  {
    sched_queue_prepare();
  }

  while (!wl_should_close()) {
    sched_queue_run();
    if (wl_draw_begin()) {
      render();
      wl_draw_end();
    }
    sched_queue_prepare();
  }
}

static void destroy_zone_private(struct zone_private* zone_private) {
  list_remove(&zone_private->link);

  free(zone_private->zone.image_buffer);
  free(zone_private);
}

void bar_cleanup(void) {
  struct widget *widget, *next_widget;
  struct zone_private *zone_private, *next_zone_private;

  list_for_each_safe(widget, next_widget, &g_widgets, link) {
    module_cleanup(widget->instance);
    free(widget);
  }

  list_for_each_safe(zone_private, next_zone_private, &g_bar.zones, link)
    destroy_zone_private(zone_private);

  sched_cleanup();
  font_cleanup();
  wl_cleanup();
}

static void alloc_zone_buffer(struct zone* zone) {
  zone->image_buffer =
    zalloc(zone->width * zone->height * sizeof(*zone->image_buffer));
  ASSERT(zone->image_buffer != NULL);
}

struct zone* bar_alloc_zone(enum zone_position position, u32 size) {
  struct zone_private* zone_private;
  struct zone* zone;

  ASSERT(position <= ZONE_POSITION_MAX);

  zone_private = zalloc(sizeof(*zone_private));
  ASSERT(zone_private != NULL);

  {
    zone_private->redraw = false;
    zone_private->offset = g_bar.sizes[position];
    list_insert(&g_bar.zones, &zone_private->link);
  }

  g_bar.sizes[position] += size;

  zone = &zone_private->zone;
  /* NOTE: This works for bars that are horizontal. For vertical bars, the
   *       dimensions should be reversed.
   */
  {
    zone->position = position;
    zone->width = size;
    zone->height = g_bar.thickness;
  }
  alloc_zone_buffer(zone);

  return zone;
}

void bar_destroy_zone(struct zone** zonep) {
  struct zone_private* zone_private;

  ASSERT(zonep != NULL);
  ASSERT(*zonep != NULL);

  zone_private = ZONE_PRIVATE(*zonep);
  destroy_zone_private(zone_private);

  *zonep = NULL;
}

static inline void clear_leftovers(enum zone_position position,
                                   u32 old_size, u32 new_size) {
  u32 c = g_bar.background_color.as_u32,
      s = old_size - new_size,
      w = old_size;

  if (old_size <= new_size)
    return;

  switch (position) {
    case ZONE_POSITION_LEFT:
      wl_clear_horizontal_region(position, w, new_size, s, c);
      break;
    case ZONE_POSITION_CENTER:
      wl_clear_horizontal_region(position, w, 0, s >> 1, c);
      wl_clear_horizontal_region(position, w, old_size - (s >> 1), s >> 1, c);
      break;
    case ZONE_POSITION_RIGHT:
      wl_clear_horizontal_region(position, w, 0, s, c);
      break;
    case ZONE_POSITION_MAX:
      log_fatal("invalid zone position %d", position);
  }
}

void bar_resize_zone(struct zone* zone, size_t size) {
  enum zone_position pos;
  u32 prev_size;
  struct zone_private* zptr = ZONE_PRIVATE(zone);

  pos = zone->position;
  /* NOTE: This works for bars that are horizontal. For vertical bars, it
   *       should be zone->height = size.
   */
  zone->width = size;
  free(zone->image_buffer);
  alloc_zone_buffer(zone);

  prev_size = g_bar.sizes[pos];
  /* Recompute offset for each zone */
  g_bar.sizes[pos] = 0;
  /* NOTE: We go through the list in reverse, because every new zone is added
   *       to the head of the list (see wl_list(3)).
   */
  list_for_each_reverse(zptr, &g_bar.zones, link) {
    if (zptr->zone.position == pos) {
      zptr->offset = g_bar.sizes[pos];
      /* Force redraw */
      zptr->redraw = true;
      /* NOTE: This works for bars that are horizontal. For vertical bars, it
       *       should be g_bar.sizes[pos] += zptr->zone.height.
       */
      g_bar.sizes[pos] += zptr->zone.width;
    }
  }

  clear_leftovers(pos, prev_size, g_bar.sizes[pos]);
}

void zone_request_redraw(struct zone* zone) {
  ASSERT(zone != NULL);
  ZONE_PRIVATE(zone)->redraw = true;
}

b8 zone_should_redraw(struct zone* zone) {
  ASSERT(zone != NULL);
  return ZONE_PRIVATE(zone)->redraw;
}
