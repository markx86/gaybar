#include <gaybar/module.h>
#include <gaybar/config.h>
#include <gaybar/list.h>
#include <gaybar/util.h>

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

struct module_instance* module_init(struct module* module,
                                    enum zone_position position) {
  struct module_instance* instance;
  struct module_init_data init_data;
  void* instance_data;

  ASSERT(module != NULL);
  if (module->callbacks.init == NULL)
    return NULL;

  init_data.position = position;
  init_data.background_color = bar_get_background_color();
  init_data.foreground_color = bar_get_foreground_color();

  config_parse_object_with_key("colors") {
   init_data.background_color =
     config_get_color_by_key("background", init_data.background_color);
   init_data.foreground_color =
     config_get_color_by_key("foreground", init_data.foreground_color);
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
