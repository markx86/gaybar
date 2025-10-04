#include <gaybar/config.h>
#include <gaybar/types.h>
#include <gaybar/log.h>
#include <gaybar/assert.h>
#include <gaybar/params.h>
#include <gaybar/util.h>
#include <gaybar/compiler.h>

#include <cJSON/cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define JSON_PATH_INDEX_MAX_LENGTH 16

/* Needed because we cast a void* -> uintptr_t -> double */
STATIC_ASSERT(sizeof(void*) == sizeof(uintptr_t));
STATIC_ASSERT(sizeof(uintptr_t) == sizeof(double));

static cJSON* g_root;
static char g_config_path[PATH_MAX];

struct config_node {
  char* name;
  struct config_node* parent;
  cJSON* json;
};

#define PATH_SPRINTF(x, ...) \
  snprintf(g_config_path, sizeof(g_config_path), x, ##__VA_ARGS__)

#define CONFIG_PATH "gaybar/config.jsonc"

static inline cJSON* unwrap_config_node(struct config_node* node) {
  return node == CONFIG_ROOT ? g_root : node->json;
}

static inline b8 can_read_config_file(void) {
  return access(g_config_path, R_OK) == 0;
}

static b8 load_config_file_path(void) {
  const char *config_home, *home, *user;

  if (g_params.config_file != NULL) {
    PATH_SPRINTF("%s", g_params.config_file);
    if (can_read_config_file())
      return true;
    else {
      log_error("could not access file '%s'", g_params.config_file);
      /* FIXME: Maybe we should fall through and continue looking for a
       *        config file?
       */
      return false;
    }
  }

  if ((config_home = getenv("XDG_CONFIG_HOME")) != NULL) {
    PATH_SPRINTF("%s/" CONFIG_PATH, config_home);
    if (can_read_config_file())
      return true;
  }

  if ((home = getenv("HOME")) != NULL) {
    PATH_SPRINTF("%s/.config/" CONFIG_PATH, home);
    if (can_read_config_file())
      return true;
  }

  if ((user = getenv("USER")) != NULL) {
    PATH_SPRINTF("/home/%s/.config/" CONFIG_PATH, user);
    if (can_read_config_file())
      return true;
  }

  return false;
}

static char* load_config_file(size_t* buffer_size) {
  int fd;
  char* buffer;
  struct stat statbuf;

  fd = open(g_config_path, O_RDONLY);
  ASSERT(fd > 0 && "could not open config file");

  ASSERT(fstat(fd, &statbuf) == 0 && "could not stat config file");

  buffer =
    mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
  ASSERT(buffer != MAP_FAILED && "could not map config file");

  *buffer_size = statbuf.st_size;
  return buffer;
}

static void unload_config_file(char* buffer, size_t buffer_size) {
  ASSERT(munmap(buffer, buffer_size) == 0);
}

void config_load(void) {
  char* content;
  size_t content_size;

  if (!load_config_file_path()) {
    log_info("could not find config file, running with default options");
    return;
  }

  content = load_config_file(&content_size);

  g_root = cJSON_ParseWithLength(content, content_size);
  if (g_root == NULL)
    /* FIXME: Print where the error is */
    log_error("could not parse config file");

  unload_config_file(content, content_size);
}

void config_unload(void) {
  cJSON_Delete(g_root);
}

static inline b8 is_int(double x) {
  return (x - (double)((i64)x)) == 0.0;
}

static b8 is_same_type(struct _config_param* param, cJSON* json) {
  switch (param->type) {
    case _CONFIG_PARAM_TYPE_INTEGER:
      return cJSON_IsNumber(json) &&
             is_int(cJSON_GetNumberValue(json));
    case _CONFIG_PARAM_TYPE_FLOAT:
      return cJSON_IsNumber(json);
    case _CONFIG_PARAM_TYPE_STRING:
      return cJSON_IsString(json);
    case _CONFIG_PARAM_TYPE_BOOL:
      return cJSON_IsBool(json);
    case _CONFIG_PARAM_TYPE_ARRAY:
      return cJSON_IsArray(json);

    default:
      /* FIXME: Print full json path of parameter */
      log_fatal("invalid configuration parameter type #%d for parameter %s",
                param->type, param->name);
  }
}

static const char* param_type_string(struct _config_param* param) {
  switch (param->type) {
    case _CONFIG_PARAM_TYPE_INTEGER:
      return "INTEGER";
    case _CONFIG_PARAM_TYPE_FLOAT:
      return "FLOAT";
    case _CONFIG_PARAM_TYPE_STRING:
      return "STRING";
    case _CONFIG_PARAM_TYPE_BOOL:
      return "BOOL";
    case _CONFIG_PARAM_TYPE_ARRAY:
      return "ARRAY";
    default:
      return "(invalid type)";
  }
}

static const char* json_type_string(cJSON* json) {
  if (cJSON_IsNumber(json))
    return is_int(cJSON_GetNumberValue(json)) ? "INTEGER"
                                              : "FLOAT";
  else if (cJSON_IsBool(json))
    return "BOOL";
  else if (cJSON_IsString(json))
    return "STRING";
  else if (cJSON_IsArray(json))
    return "ARRAY";
  else
    return "(invalid type)";
}

static void store_param_value(struct _config_param* param, void* value) {
  switch (param->type) {
  case _CONFIG_PARAM_TYPE_INTEGER:
    *((long*)param->store) = (long)value;
    break;

  case _CONFIG_PARAM_TYPE_FLOAT:
    *((double*)param->store) = *(double*)&value;
    break;

  case _CONFIG_PARAM_TYPE_STRING:
    *((char**)param->store) = value != NULL ? strdup(value) : NULL;
    break;

  default:
    ASSERT(false && "unreachable");
  }
}

static void get_json_value(cJSON* item, void** out) {
  double number;

  if (cJSON_IsNumber(item)) {
    number = cJSON_GetNumberValue(item);
    *out = (void*)
      (is_int(cJSON_GetNumberValue(item)) ? (intptr_t)number
                                          : *(intptr_t*)&number);
  }
  else if (cJSON_IsBool(item))
    *out = (void*)(uintptr_t)(cJSON_IsTrue(item) ? true : false);
  else if (cJSON_IsString(item))
    *out = cJSON_GetStringValue(item);
  else
    ASSERT(false && "unreachable");
}

static void array_parse(struct _config_param* param, cJSON* array) {
  size_t i;
  cJSON* elem;
  config_array_parse_callback_t cb;

  cb = param->store;
  ASSERT(cb != NULL);

  i = 0;
  cJSON_ArrayForEach(elem, array)
    cb(i++, &((struct config_node) { .json = elem }));
}

static void array_empty(struct _config_param* param) {
  config_array_empty_callback_t cb = param->default_value;
  if (param->has_default_value) {
    ASSERT(cb != NULL);
    cb();
  }
}

static inline cJSON* item_by_name(cJSON* container, const char* name) {
  return name == CONFIG_PARAM_SELF ? container
                                   : cJSON_GetObjectItem(container, name);
}

static b8 parse_parameter(cJSON* container, struct _config_param* param) {
  cJSON* item;
  void* value;

  item = item_by_name(container, param->name);
  if (item == NULL) {
try_store_default_value:
    /* That parameter has not value specified in the configuration file */
    if (param->type == _CONFIG_PARAM_TYPE_ARRAY)
      array_empty(param);
    else if (param->has_default_value)
      store_param_value(param, param->default_value);
    else
      log_fatal("missing required configuration parameter '%s' of type %s",
                param->name, param_type_string(param));
    return false;
  }

  if (!is_same_type(param, item)) {
    log_error("type mismatch for configuration parameter %s", param->name);
    log_error("expected %s, but got %s",
              param_type_string(param), json_type_string(item));
    goto try_store_default_value;
  }

  if (param->type == _CONFIG_PARAM_TYPE_ARRAY)
    cJSON_GetArraySize(item) == 0 ? array_empty(param)
                                  : array_parse(param, item);
  else {
    get_json_value(item, &value);
    store_param_value(param, value);
  }

  return true;
}

size_t _config_parse(struct config_node* node,
                    struct _config_param* params, size_t params_count) {
  size_t parsed;
  struct _config_param* param;
  cJSON* json_node;

  parsed = 0;
  param = params;
  json_node = unwrap_config_node(node);

  for (; params_count > 0; --params_count) {
    parsed += parse_parameter(json_node, param);
    ++param;
  }

  return parsed;
}

struct config_node* config_get_node(struct config_node* parent,
                                    const char* name) {
  struct config_node* node = zalloc(sizeof(*node));
  ASSERT(node != NULL);
  node->name = strdup(name);
  node->parent = parent;
  node->json = cJSON_GetObjectItem(unwrap_config_node(parent), name);
  return node;
}

void config_destroy_node(struct config_node* node) {
  /* We do not delete the cJSON pointer inside the node, because it will
   * be deleted by config_unload(..) and somebody else might request the
   * a the same node.
   *
   * NOTE: Should we refcount the struct instead?
   */
  free(node->name);
  free(node);
}
