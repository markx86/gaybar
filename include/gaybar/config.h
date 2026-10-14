#ifndef CONFIG_H_
#define CONFIG_H_

#include <gaybar/types.h>
#include <gaybar/color.h>

struct config_node;

void   config_load(void);
void   config_unload(void);

#define _CONFIG_GENERIC_GETTER(type, name)                        \
  type _config_get_##name(const void* key, b8 is_index, type def)

#define _CONFIG_KEY_GETTER(type, name)                                     \
  static inline type config_get_##name##_by_key(const char* k, type def) { \
    return _config_get_##name(k, false, def);                              \
  }

#define _CONFIG_IDX_GETTER(type, name)                                \
  static inline type config_get_##name##_by_idx(size_t i, type def) { \
    return _config_get_##name((const void*)i, true, def);             \
  }

#define _CONFIG_DEF_GETTERS(type, name)    \
  _CONFIG_GENERIC_GETTER(type, name); \
  _CONFIG_KEY_GETTER(type, name);     \
  _CONFIG_IDX_GETTER(type, name)

_CONFIG_DEF_GETTERS(char*              , string);
_CONFIG_DEF_GETTERS(f64                , float);
_CONFIG_DEF_GETTERS(i64                , int);
_CONFIG_DEF_GETTERS(struct color       , color);

_CONFIG_GENERIC_GETTER(struct config_node*, object);
_CONFIG_GENERIC_GETTER(struct config_node*, array);

#undef _CONFIG_DEF_GETTERS

b8 _config_push_root(void);
b8 _config_push(struct config_node* node);
b8 _config_pop(void);

size_t config_get_array_length(void);
b8     config_has_key(const char* k);

const char* _config_get_path(const void* key, b8 is_index);

#define config_get_key_path(k) _config_get_path(k, false)
#define config_get_idx_path(k) _config_get_path(k, true)

#define config_with_root_context()      \
  for (b8 _latch = _config_push_root(); \
       _latch;                          \
       _latch = (_config_pop(), false))

#define config_parse(node) \
  for (b8 _latch = _config_push(node); _latch; _latch = (_config_pop(), false))

#define _config_parse_object(k, is_index)                               \
  for (b8 _latch = _config_push(_config_get_object(k, is_index, NULL)); \
       _latch;                                                          \
       _latch = (_config_pop(), false))

#define config_parse_object_with_key(k) \
  _config_parse_object(k, false)

#define config_parse_object_at_index(i) \
  _config_parse_object((const void*)i, true)

#define _config_parse_array(k, is_index)                               \
  for (b8 _latch = _config_push(_config_get_array(k, is_index, NULL)); \
       _latch;                                                         \
       _latch = (_config_pop(), false))

#define config_parse_array_with_key(k) \
  _config_parse_array(k, false)

#define config_parse_array_at_index(i) \
  _config_parse_array((void*)i, true)

#define CONFIG_ROOT ((struct config_node*)-100L)

#endif
