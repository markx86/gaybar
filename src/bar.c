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
#include <ctype.h>

#define BAR_DEFAULT_POSITION  "top"
#define BAR_DEFAULT_THICKNESS 32
#define BAR_DEFAULT_COLOR     "#333333"

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
  struct color color;
  u32 sizes[ZONE_POSITION_MAX];
  struct wl_list zones;
};

/* This initialization is equivalent to calling list_init(..) */
struct list g_modules = { .next = &g_modules, .prev = &g_modules };

static struct bar g_bar = {0};

enum bar_position bar_get_position(void) { return g_bar.position; }
u32 bar_get_thickness(void) { return g_bar.thickness; }

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

static u8 hex_char_to_u8(char c) {
  ASSERT(isxdigit(c));

  if (c >= '0' && c <= '9')
    return c - '0';
  else if (c >= 'A'&& c <= 'F')
    return (c - 'A') + 10;
  else if (c >= 'a'&& c <= 'f')
    return (c - 'a') + 10;
  else
    ASSERT(false && "unreachable");
}

static struct color color_from_hex(const char* s) {
  size_t i, l;
  struct color c;

  ASSERT(s != NULL);

  l = strlen(s);

  if (*s != '#')
    goto fail;
  if (l - 1 != 6)
    goto fail;

  for (i = 1; i < l; ++i) {
    if (!isxdigit(s[i]))
      goto fail;
  }

  c.r = (hex_char_to_u8(s[1]) << 4) | hex_char_to_u8(s[2]);
  c.g = (hex_char_to_u8(s[3]) << 4) | hex_char_to_u8(s[4]);
  c.b = (hex_char_to_u8(s[5]) << 4) | hex_char_to_u8(s[6]);
  c.a = 0xFF;

  return c;
fail:
  log_error("invalid color '%s', must be in RGB HEX format", s);
  return color_from_hex(BAR_DEFAULT_COLOR);
}

static void init_module_from_config(struct config_node* node,
                                    enum zone_position position) {
  int rc;
  char* module_name;
  struct module* module;

  CONFIG_PARSE(node,
    CONFIG_PARAM(
      CONFIG_PARAM_NAME(CONFIG_PARAM_SELF),
      CONFIG_PARAM_TYPE(STRING),
      CONFIG_PARAM_STORE(module_name)
    )
  );
  ASSERT(module_name != NULL);

  module = module_find_by_name(module_name);
  if (module == NULL) {
    log_error("no module named '%s' found", module_name);
    goto out;
  }

  rc = module_init(module, position);
  if (rc < 0) {
    log_error("could not initialize module '%s' (failed with error code %d)",
              module->name, rc);
    module_cleanup(module);
  }

out:
  free(module_name);
}

static void init_left_side_module(size_t index, struct config_node* node) {
  UNUSED(index);
  init_module_from_config(node, ZONE_POSITION_LEFT);
}

static void init_center_module(size_t index, struct config_node* node) {
  UNUSED(index);
  init_module_from_config(node, ZONE_POSITION_CENTER);
}

static void init_right_side_module(size_t index, struct config_node* node) {
  UNUSED(index);
  init_module_from_config(node, ZONE_POSITION_RIGHT);
}

static void init_modules(void) {
  struct config_node* modules_node = config_get_node(CONFIG_ROOT, "modules");
  CONFIG_PARSE(modules_node,
    CONFIG_PARAM(
      CONFIG_PARAM_NAME("left"),
      CONFIG_PARAM_TYPE(ARRAY),
      CONFIG_PARAM_STORE(init_left_side_module),
    )
  );
  CONFIG_PARSE(modules_node,
    CONFIG_PARAM(
      CONFIG_PARAM_NAME("left"),
      CONFIG_PARAM_TYPE(ARRAY),
      CONFIG_PARAM_STORE(init_center_module),
    )
  );
  CONFIG_PARSE(modules_node,
    CONFIG_PARAM(
      CONFIG_PARAM_NAME("left"),
      CONFIG_PARAM_TYPE(ARRAY),
      CONFIG_PARAM_STORE(init_right_side_module),
    )
  );
  config_destroy_node(modules_node);
}

int bar_init(void) {
  int rc;
  enum bar_position position;
  char *position_value, *color_value;
  long thickness;
  struct color color;

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
    ),
    CONFIG_PARAM(
      CONFIG_PARAM_NAME("color"),
      CONFIG_PARAM_TYPE(STRING),
      CONFIG_PARAM_STORE(color_value),
      CONFIG_PARAM_DEFAULT(BAR_DEFAULT_COLOR)
    )
  );

  position = position_from_string(position_value);
  free(position_value);

  if (thickness < 0) {
    log_error("thickness must be positive (got %ld)", thickness);
    thickness = BAR_DEFAULT_THICKNESS;
  }

  color = color_from_hex(color_value);
  free(color_value);

  log_trace("creating bar anchored on the %s of the screen with thickness %u "
            "and color #%02hhx%02hhx%02hhx",
            position_string(position), thickness, color.r, color.g, color.b);

  {
    g_bar.position = position;
    g_bar.thickness = thickness;
    g_bar.color = color;
    list_init(&g_bar.zones);
  }

  rc = font_init();
  if (rc < 0)
    goto out;

  rc = wl_init();
  if (rc < 0)
    goto out;

  sched_init();

  init_modules();

  /* Clear the bar */
  {
    while (!wl_draw_begin())
      ;
    wl_clear(g_bar.color.as_u32);
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
  struct module *module, *next_module;
  struct zone_private *zone_private, *next_zone_private;

  list_for_each_safe(module, next_module, &g_modules, link)
    module_cleanup(module);

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
