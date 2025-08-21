#ifndef CONFIG_H_
#define CONFIG_H_

#include <gaybar/types.h>

struct config_node;

typedef void (*config_array_parse_callback_t)(size_t index,
                                              struct config_node* elem);
typedef void (*config_array_empty_callback_t)(void);

/* NOTE: The value stored in parameters of type STRING is a pointer to
 *       heap memory allocated via strdup(..). This means that, once you're done
 *       with it, you should free memory using free(..).
 */
enum config_param_type {
  CONFIG_PARAM_TYPE_INVALID = 0,
                             /* config_param.store must be of type */
  CONFIG_PARAM_TYPE_INTEGER, /* long*                              */
  CONFIG_PARAM_TYPE_FLOAT,   /* double*                            */
  CONFIG_PARAM_TYPE_STRING,  /* char**                             */
  CONFIG_PARAM_TYPE_BOOL,    /* bool*                              */
  CONFIG_PARAM_TYPE_ARRAY,   /* config_array_parse_callback_t      */
  CONFIG_PARAM_TYPE_MAX
};

struct config_param {
  b8 has_default_value;
  enum config_param_type type;
  const char* name;
  void* store;
  void* default_value;
};

void   config_load(void);
void   config_unload(void);
size_t config_parse(struct config_node* node,
                    struct config_param* params, size_t params_count);

struct config_node* config_get_node(struct config_node* node,
                                    const char* name);
void config_destroy_node(struct config_node* node);

#define CONFIG_PARSE(node, ...)                                \
  config_parse(node, (struct config_param[]) { __VA_ARGS__ },  \
               sizeof((struct config_param[]) { __VA_ARGS__ }) \
                 / sizeof(struct config_param))

#define CONFIG_PARAM(...) \
  (struct config_param) { __VA_ARGS__ }

#define CONFIG_PARAM_SELF NULL

#define CONFIG_PARAM_NAME(n)    .name = (n)
#define CONFIG_PARAM_TYPE(t)    .type = (CONFIG_PARAM_TYPE_##t)
#define CONFIG_PARAM_STORE(s)   .store = (void*)&(s)
/* Use this if the parameter type is not FLOAT */
#define CONFIG_PARAM_DEFAULT(v) \
  .has_default_value = true,    \
  .default_value = (void*)(v)
/* Use this if the parameter type is FLOAT */
#define CONFIG_PARAM_DEFAULT_FLOAT(v) \
  .has_default_value = true,          \
  .default_value = *(void**)((double[]) {(v)})

#define CONFIG_ROOT NULL

#endif
