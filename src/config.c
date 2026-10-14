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

static struct config_node* g_current;
static cJSON* g_root;
static char g_config_path[PATH_MAX];

struct config_node {
  char* name;
  struct config_node* parent;
  struct config_node* prev;
  cJSON* json;
};

#define PATH_SPRINTF(x, ...) \
  snprintf(g_config_path, sizeof(g_config_path), x, ##__VA_ARGS__)

#define CONFIG_PATH "gaybar/config.jsonc"

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
    log_error("failed to parse config file");

  unload_config_file(content, content_size);

  ASSERT(_config_push_root());
}

void config_unload(void) {
  cJSON_Delete(g_root);
}

static void destroy_node(struct config_node* node) {
  if (node->name != NULL)
    free(node->name);
  free(node);
}

static char* key_to_name(const void* key, b8 is_index) {
  char buf[64];
  if (is_index)
    snprintf(buf, sizeof(buf), "[%zu]", (size_t)key);
  else
    snprintf(buf, sizeof(buf), ".%s", (const char*)key);
  return strdup(buf);
}

static char* get_node_path_recursive(struct config_node* node,
                                     char* buffer, char* const buffer_end) {
  if (node->parent != NULL)
    buffer = get_node_path_recursive(node->parent, buffer, buffer_end);
  if (node->name == NULL || buffer >= buffer_end)
    return buffer;
  else
    return stpncpy(buffer, node->name, buffer_end - buffer);
}

/* NOTE: This function is not thread safe! */
const char* _config_get_path(const void* key, b8 is_index) {
  static char tmp_buffer[0x200];

  char *buffer, *buffer_end, *node_name;

  node_name = key_to_name(key, is_index);
  ASSERT(node_name != NULL);

  buffer_end = tmp_buffer + sizeof(tmp_buffer) - 1;
  buffer = get_node_path_recursive(g_current, tmp_buffer, buffer_end);

  if (buffer < buffer_end)
    strncpy(buffer, node_name, buffer_end - buffer);
  else {
    buffer_end[-3] = '.';
    buffer_end[-2] = '.';
    buffer_end[-1] = '.';
  }

  *buffer_end = '\0';
  free(node_name);

  return tmp_buffer;
}

static inline cJSON* unwrap_node(struct config_node* node) {
  return node == CONFIG_ROOT ? g_root : node->json;
}

static struct config_node* wrap_node(struct config_node* parent,
                                     cJSON* jnode, char* name) {
  struct config_node* node = zalloc(sizeof(*node));
  ASSERT(node != NULL);
  node->parent = parent;
  node->json = jnode;
  node->name = name;
  node->prev = NULL;
  return node;
}

b8 _config_push_root(void) {
  return _config_push(wrap_node(NULL, g_root, NULL));
}

b8 _config_push(struct config_node* node) {
  if (node == NULL)
    return false;

  node->prev = g_current;
  g_current = node;

  return true;
}

b8 _config_pop(void) {
  struct config_node* popped;

  if (g_current == NULL)
    return false;

  popped = g_current;
  g_current = g_current->prev;
  destroy_node(popped);

  return true;
}

static inline b8 is_int(double x) {
  return (x - (double)((i64)x)) == 0.0;
}

static inline cJSON* get_json_node_by_key(cJSON* jnode,
                                          const void* key, b8 is_index) {
  return is_index ? cJSON_GetArrayItem(jnode, (size_t)key)
                  : cJSON_GetObjectItem(jnode, key);
}

struct config_node* _config_get_parent(struct config_node* node) {
  return node->parent;
}

size_t config_get_array_length(void) {
  cJSON* jnode = unwrap_node(g_current);
  return cJSON_IsArray(jnode) ? cJSON_GetArraySize(jnode) : 0;
}

b8 config_has_key(const char* k) {
  cJSON* jnode = unwrap_node(g_current);
  return cJSON_IsObject(jnode) && cJSON_HasObjectItem(jnode, k);
}

_CONFIG_GENERIC_GETTER(char*, string) {
  cJSON* value = get_json_node_by_key(unwrap_node(g_current), key, is_index);

  if (value == NULL || !cJSON_IsString(value))
    return def == NULL ? NULL : strdup(def);
  else
    return strdup(cJSON_GetStringValue(value));
}

_CONFIG_GENERIC_GETTER(f64, float) {
  cJSON* value = get_json_node_by_key(unwrap_node(g_current), key, is_index);

  if (value == NULL || !cJSON_IsNumber(value))
    return def;
  else
    return cJSON_GetNumberValue(value);
}

_CONFIG_GENERIC_GETTER(i64, int) {
  f64 val;
  cJSON* value = get_json_node_by_key(unwrap_node(g_current), key, is_index);

  if (value == NULL || !cJSON_IsNumber(value))
    return def;
  else
    return is_int(val = cJSON_GetNumberValue(value)) ? (i64)val : def;
}

_CONFIG_GENERIC_GETTER(struct config_node*, object) {
  cJSON* value = get_json_node_by_key(unwrap_node(g_current), key, is_index);
  if (value == NULL || !cJSON_IsObject(value))
    return def;
  else
    return wrap_node(g_current, value, key_to_name(key, is_index));
}

_CONFIG_GENERIC_GETTER(struct config_node*, array) {
  cJSON* value = get_json_node_by_key(unwrap_node(g_current), key, is_index);
  if (value == NULL || !cJSON_IsArray(value))
    return def;
  else
    return wrap_node(g_current, value, key_to_name(key, is_index));
}

_CONFIG_GENERIC_GETTER(struct color, color) {
  struct color color;
  cJSON* value = get_json_node_by_key(unwrap_node(g_current), key, is_index);

  if (value == NULL || !cJSON_IsString(value))
    return def;

  return color_from_hex(cJSON_GetStringValue(value), &color) ? color : def;
}
