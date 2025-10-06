#include "gaybar/util.h"
#include <gaybar/module.h>
#include <gaybar/list.h>

#include <stdlib.h>
#include <string.h>

struct module_instance {
  struct module* module;
  void* instance_data;
};

struct list g_modules = LIST_UNINITIALIZED;

struct module* module_find_by_name(const char* name) {
  struct module* module;
  list_for_each(module, &g_modules, link) {
    if (strncmp(module->name, name, strcspn(name, "#")) == 0)
      return module;
  }
  return NULL;
}

static b8 parse_color_and_free(char* color_hex, struct color* out_color) {
  b8 result = color_from_hex(color_hex, out_color);
  free(color_hex);
  return result;
}

static void get_colors_from_config(struct config_node* colors,
                                   struct color* background_color,
                                   struct color* foreground_color) {
  char *foreground_color_hex, *background_color_hex;

  CONFIG_PARSE(colors,
    CONFIG_PARAM(
      CONFIG_PARAM_NAME("background"),
      CONFIG_PARAM_TYPE(STRING),
      CONFIG_PARAM_STORE(background_color_hex),
      CONFIG_PARAM_DEFAULT(NULL)
    ),
    CONFIG_PARAM(
      CONFIG_PARAM_NAME("foreground"),
      CONFIG_PARAM_TYPE(STRING),
      CONFIG_PARAM_STORE(foreground_color_hex),
      CONFIG_PARAM_DEFAULT(NULL)
    )
  );

  if (foreground_color_hex == NULL ||
      !parse_color_and_free(foreground_color_hex, foreground_color))
    *foreground_color = bar_get_foreground_color();

  if (background_color_hex == NULL ||
      !parse_color_and_free(background_color_hex, background_color))
    *background_color = bar_get_background_color();
}

struct module_instance* module_init(struct module* module,
                                    struct config_node* config,
                                    enum zone_position position) {
  struct module_instance* instance;
  struct module_init_data init_data;
  void* instance_data;
  struct config_node* colors;

  ASSERT(module != NULL);
  if (module->callbacks.init == NULL)
    return NULL;

  init_data.config = config;
  init_data.position = position;

  if (config == NULL) {
    init_data.background_color = bar_get_background_color();
    init_data.foreground_color = bar_get_foreground_color();
  } else {
    colors = config_get_node(config, "colors");
    get_colors_from_config(colors,
                           &init_data.background_color,
                           &init_data.foreground_color);
    config_destroy_node(colors);
  }

  instance_data = module->callbacks.init(&init_data);
  if (instance_data == MODULE_INIT_FAIL)
    return NULL;

  instance = zalloc(sizeof(*instance));
  ASSERT(instance != NULL);

  instance->instance_data = instance_data;
  instance->module = module;

  return instance;
}

void module_render(struct module_instance* instance) {
  ASSERT(instance != NULL);
  if (instance->module->callbacks.render == NULL)
    log_warn("render requested for module %s, but it has no render method!",
             instance->module->name);
  else
    instance->module->callbacks.render(instance->instance_data);
}

void module_cleanup(struct module_instance* instance) {
  ASSERT(instance != NULL);
  if (instance->module->callbacks.cleanup != NULL)
    instance->module->callbacks.cleanup(instance->instance_data);
  free(instance);
}
