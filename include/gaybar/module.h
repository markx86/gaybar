#ifndef MODULE_H_
#define MODULE_H_

#include <gaybar/types.h>
#include <gaybar/list.h>
#include <gaybar/compiler.h>
#include <gaybar/assert.h>
#include <gaybar/config.h>
#include <gaybar/bar.h>

struct module_callbacks {
  void* (*init)(enum zone_position position, struct config_node* config);
  void  (*render)(void* instance);
  void  (*cleanup)(void* instance);
};

struct module {
  struct list link;
  const char* name;
  const char* author;
  const char* description;
  struct module_callbacks callbacks;
};

struct module_instance;

extern struct list g_modules;

struct module* module_find_by_name(const char* name);

struct module_instance* module_init(struct module* module,
                                    struct config_node* config,
                                    enum zone_position position);
void module_render(struct module_instance* instance);
void module_cleanup(struct module_instance* instance);

#define MODULE(_name, _author, _description) \
  STATIC_ASSERT(_name != NULL);              \
  static struct module g_module = {          \
    .name = _name,                           \
    .author = _author,                       \
    .description = _description,             \
    .callbacks = {0}                         \
  }

#define MODULE_CALLBACKS(...)                         \
  static void CONSTRUCTOR _register_module(void) {    \
    g_module.callbacks = (struct module_callbacks) {  \
      __VA_ARGS__                                     \
    };                                                \
    if (!list_is_initialized(&g_modules))             \
      list_init(&g_modules);                          \
    list_insert(&g_modules, &g_module.link);          \
  }                                                   \

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
