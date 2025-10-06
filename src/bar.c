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
      wl_draw_zone(&zone_private->zone, zone_private->offset,
                   g_bar.sizes[zone_private->zone.position]);
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

static void init_widget_from_config(struct config_node* node,
                                    enum zone_position position) {
  char* widget_name;
  struct config_node* config;
  struct module* module;
  struct module_instance* instance;
  struct widget* widget;

  CONFIG_PARSE(node,
    CONFIG_PARAM(
      CONFIG_PARAM_NAME(CONFIG_PARAM_SELF),
      CONFIG_PARAM_TYPE(STRING),
      CONFIG_PARAM_STORE(widget_name)
    )
  );
  ASSERT(widget_name != NULL);

  module = module_find_by_name(widget_name);
  if (module == NULL) {
    log_error("no module widget '%s' found", widget_name);
    goto out_module_name;
  }

  config = config_get_node(CONFIG_ROOT, widget_name);

  instance = module_init(module, config, position);
  if (instance == NULL) {
    log_error("could not initialize module '%s'",
              widget_name);
    goto out_config;
  }

  widget = zalloc(sizeof(*widget));
  ASSERT(widget != NULL);

  widget->instance = instance;
  list_insert(&g_widgets, &widget->link);

out_config:
  config_destroy_node(config);
out_module_name:
  free(widget_name);
}

static void init_left_side_widget(size_t index, struct config_node* node) {
  UNUSED(index);
  init_widget_from_config(node, ZONE_POSITION_LEFT);
}

static void init_center_widget(size_t index, struct config_node* node) {
  UNUSED(index);
  init_widget_from_config(node, ZONE_POSITION_CENTER);
}

static void init_right_side_widget(size_t index, struct config_node* node) {
  UNUSED(index);
  init_widget_from_config(node, ZONE_POSITION_RIGHT);
}

static void init_widgets(void) {
  struct config_node* widgets_node;

  list_init(&g_widgets);

  widgets_node = config_get_node(CONFIG_ROOT, "widgets");
  CONFIG_PARSE(widgets_node,
    CONFIG_PARAM(
      CONFIG_PARAM_NAME("left"),
      CONFIG_PARAM_TYPE(ARRAY),
      CONFIG_PARAM_STORE(init_left_side_widget),
    )
  );
  CONFIG_PARSE(widgets_node,
    CONFIG_PARAM(
      CONFIG_PARAM_NAME("center"),
      CONFIG_PARAM_TYPE(ARRAY),
      CONFIG_PARAM_STORE(init_center_widget),
    )
  );
  CONFIG_PARSE(widgets_node,
    CONFIG_PARAM(
      CONFIG_PARAM_NAME("right"),
      CONFIG_PARAM_TYPE(ARRAY),
      CONFIG_PARAM_STORE(init_right_side_widget),
    )
  );
  config_destroy_node(widgets_node);
}

static void get_colors(struct color* background_color,
                       struct color* foreground_color) {
  struct config_node* colors;
  char *foreground_color_hex, *background_color_hex;

  colors = config_get_node(CONFIG_ROOT, "colors");
  if (colors == NULL) {
    background_color_hex = BAR_DEFAULT_COLOR_BACKGROUND;
    foreground_color_hex = BAR_DEFAULT_COLOR_FOREGROUND;
  } else {
    CONFIG_PARSE(colors,
      CONFIG_PARAM(
        CONFIG_PARAM_NAME("foreground"),
        CONFIG_PARAM_TYPE(STRING),
        CONFIG_PARAM_STORE(foreground_color_hex),
        CONFIG_PARAM_DEFAULT(BAR_DEFAULT_COLOR_FOREGROUND)
      ),
      CONFIG_PARAM(
        CONFIG_PARAM_NAME("background"),
        CONFIG_PARAM_TYPE(STRING),
        CONFIG_PARAM_STORE(background_color_hex),
        CONFIG_PARAM_DEFAULT(BAR_DEFAULT_COLOR_BACKGROUND)
      )
    );
  }

  if (!color_from_hex(background_color_hex, background_color))
    ASSERT(color_from_hex(BAR_DEFAULT_COLOR_BACKGROUND, background_color));
  if (!color_from_hex(foreground_color_hex, foreground_color))
    ASSERT(color_from_hex(BAR_DEFAULT_COLOR_FOREGROUND, foreground_color));

  /* If the config node is not NULL then these values have been strduped
   * by CONFIG_PARSE(..).
   */
  if (colors != NULL) {
    free(background_color_hex);
    free(foreground_color_hex);
  }

  config_destroy_node(colors);
}

int bar_init(void) {
  int rc;
  enum bar_position position;
  char* position_value;
  long thickness;
  struct color background_color, foreground_color;

  CONFIG_PARSE(CONFIG_ROOT,
    CONFIG_PARAM(
      CONFIG_PARAM_NAME("position"),
      CONFIG_PARAM_TYPE(STRING),
      CONFIG_PARAM_STORE(position_value),
      CONFIG_PARAM_DEFAULT(BAR_DEFAULT_POSITION)
    ),
    CONFIG_PARAM(
      CONFIG_PARAM_NAME("thickness"),
      CONFIG_PARAM_TYPE(INTEGER),
      CONFIG_PARAM_STORE(thickness),
      CONFIG_PARAM_DEFAULT(BAR_DEFAULT_THICKNESS)
    )
  );

  position = position_from_string(position_value);
  free(position_value);

  if (thickness < 0) {
    log_error("thickness must be positive (got %ld)", thickness);
    thickness = BAR_DEFAULT_THICKNESS;
  }

  get_colors(&background_color, &foreground_color);

  log_trace("creating bar anchored on the %s of the screen with thickness %u, "
            "background color #%02hhx%02hhx%02hhx and "
            "foreground color #%02hhx%02hhx%02hhx",
            position_string(position), thickness,
            background_color.r, background_color.g, background_color.b,
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
  zone->image_buffer =
    zalloc(size * g_bar.thickness * sizeof(*zone->image_buffer));
  ASSERT(zone->image_buffer != NULL);

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

void zone_request_redraw(struct zone* zone) {
  ASSERT(zone != NULL);
  ZONE_PRIVATE(zone)->redraw = true;
}

b8 zone_should_redraw(struct zone* zone) {
  ASSERT(zone != NULL);
  return ZONE_PRIVATE(zone)->redraw;
}
