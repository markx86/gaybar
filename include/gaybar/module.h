#ifndef MODULE_H_
#define MODULE_H_

#include <gaybar/types.h>
#include <gaybar/list.h>
#include <gaybar/compiler.h>
#include <gaybar/assert.h>

struct module_callbacks {
  int  (*init)(void);
  void (*render)(void);
  void (*cleanup)(void);
};

struct module {
  struct list link;
  const char* name;
  const char* author;
  const char* description;
  struct module_callbacks callbacks;
};

extern struct list g_modules;

static inline int module_init(struct module* module) {
  ASSERT(module != NULL);
  return module->callbacks.init == NULL ? 0 : module->callbacks.init();
}

static inline void module_render(struct module* module) {
  ASSERT(module != NULL);
  if (module->callbacks.render == NULL)
    log_warn("render requested for module %s, but it has no render method!",
             module->name);
  else
    module->callbacks.render();
}

static inline void module_cleanup(struct module* module) {
  ASSERT(module != NULL);
  if (module->callbacks.cleanup != NULL) { module->callbacks.cleanup(); }
  list_remove(&module->link);
}

#define MODULE(n, a, d)                       \
  static struct module g_module = {           \
    .name = n, .author = a, .description = d, \
    .callbacks = {0}                          \
  }

#define MODULECALLBACKS(...)                          \
  static CONSTRUCTOR void __register_module(void) {   \
    g_module.callbacks = (struct module_callbacks) {  \
      __VA_ARGS__                                     \
    };                                                \
    list_insert(&g_modules, &g_module.link);          \
  }

#define module_trace(x, ...) \
  log_trace("%s: " x, g_module.name, ##__VA_ARGS__)
#define module_info(x, ...) \
  log_info("%s: " x, g_module.name, ##__VA_ARGS__)
#define module_warn(x, ...) \
  log_warn("%s: " x, g_module.name, ##__VA_ARGS__)
#define module_error(x, ...) \
  log_error("%s: " x, g_module.name, ##__VA_ARGS__)
#define module_fatal(x, ...) \
  log_fatal("%s: " x, g_module.name, ##__VA_ARGS__)

#endif
