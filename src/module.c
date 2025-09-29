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

struct module_instance* module_init(struct module* module,
                                    struct config_node* config,
                                    enum zone_position position) {
  struct module_instance* instance;

  ASSERT(module != NULL);
  if (module->callbacks.init == NULL)
    return NULL;

  instance = zalloc(sizeof(*instance));
  ASSERT(instance != NULL);

  instance->instance_data = module->callbacks.init(position, config);
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
