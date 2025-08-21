#ifndef MODULE_H_
#define MODULE_H_

#include <gaybar/types.h>
#include <gaybar/list.h>
#include <gaybar/compiler.h>
#include <gaybar/assert.h>
#include <gaybar/config.h>
#include <gaybar/bar.h>

struct module_callbacks {
  int  (*init)(enum zone_position position, struct config_node*);
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

int  module_init(struct module* module, enum zone_position position);
void module_render(struct module* module);
void module_cleanup(struct module* module);

struct module* module_find_by_name(const char* name);

#define MODULE(n, a, d)                       \
  static struct module g_module = {           \
    .name = n, .author = a, .description = d, \
    .callbacks = {0}                          \
  }

#define MODULE_CALLBACKS(...)                         \
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
