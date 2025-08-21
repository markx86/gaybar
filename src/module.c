#include <gaybar/module.h>
#include <gaybar/list.h>

#include <string.h>

int module_init(struct module* module, enum zone_position position) {
  struct config_node* node;
  int rc;

  ASSERT(module != NULL);
  if (module->callbacks.init == NULL)
    return 0;

  node = config_get_node(CONFIG_ROOT, module->name);
  rc = module->callbacks.init(position, node);

  config_destroy_node(node);
  return rc;
}

void module_render(struct module* module) {
  ASSERT(module != NULL);
  if (module->callbacks.render == NULL)
    log_warn("render requested for module %s, but it has no render method!",
             module->name);
  else
    module->callbacks.render();
}

void module_cleanup(struct module* module) {
  ASSERT(module != NULL);
  if (module->callbacks.cleanup != NULL) { module->callbacks.cleanup(); }
  list_remove(&module->link);
}

struct module* module_find_by_name(const char* name) {
  struct module* module;
  list_for_each(module, &g_modules, link) {
    if (module->name != NULL && strcmp(module->name, name) == 0)
      return module;
  }
  return NULL;
}
