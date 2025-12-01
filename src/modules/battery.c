#include <gaybar/module.h>
#include <gaybar/bar.h>
#include <gaybar/util.h>
#include <gaybar/draw.h>
#include <gaybar/sched.h>
#include <gaybar/font.h>
#include <gaybar/format.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BATTERY_PATH "/sys/class/power_supply/%s/uevent"

#define BATTERY_DEFAULT_NAME       "BAT1"
#define BATTERY_DEFAULT_FORMAT     "{icon} {charge-percentage}%"
#define BATTERY_DEFAULT_FORMAT_ALT "{icon} {time}"

#define REFRESH_INTERVAL 1.0f /* In seconds */

#define PREFIX_CHARGE "POWER_SUPPLY_CHARGE_"
#define PREFIX_ENERGY "POWER_SUPPLY_ENERGY_"

#define AVG_SAMPLES 60

enum acpi_mode {
  ACPI_MODE_UNKNOWN,
  ACPI_MODE_CHARGE,
  ACPI_MODE_ENERGY
};

enum battery_status {
  BATTERY_STATUS_UNKNOWN,
  BATTERY_STATUS_DISCHARGING,
  BATTERY_STATUS_CHARGING,
};

struct battery_info {
  enum acpi_mode mode;
  enum battery_status status;
  u64 percentage;
  u64 capacity_max;
  u64 capacity_now;
  u64 consumption_avg;
};

struct battery_samples {
  size_t index;
  u64 values[AVG_SAMPLES];
};

struct battery_icons {
  char** array;
  size_t length;
};

struct battery_instance {
  struct list link;
  char* name;
  char *format, *format_alt;
  struct battery_icons icons;
  int uevent_fd;
  struct battery_info info;
  struct battery_samples consumption_ring;
  struct zone* zone;
  struct color bg_color;
  struct color fg_color;
};

static i64 g_task_id = -1;
static struct list g_instances = LIST_UNINITIALIZED;

MODULE("battery", "markx86", "Display battery information.");

static void reset_consumption_ring(struct battery_instance* instance) {
  struct battery_samples* ring = &instance->consumption_ring;
  ring->index = 0;
  memset(ring->values, 0xFF, sizeof(ring->values));
}

static void fill_consumption_ring(struct battery_instance* instance) {
  u64 i;
  struct battery_samples* ring = &instance->consumption_ring;
  for (i = 0; i < ARRAY_LENGTH(ring->values); ++i)
    ring->values[i] = ring->values[0];
}

static const char* battery_mode(struct battery_instance* instance) {
  struct battery_info* info = &instance->info;
  if (info->mode == ACPI_MODE_CHARGE)
    return "charge";
  else if (info->mode == ACPI_MODE_ENERGY)
    return "energy";
  else
    return "(unknown)";
}

static const char* battery_status(struct battery_instance* instance) {
  struct battery_info* info = &instance->info;
  if (info->status == BATTERY_STATUS_CHARGING)
    return "charging";
  else if (info->status == BATTERY_STATUS_DISCHARGING)
    return "discharging";
  else
    return "(unknown)";
}

static char battery_unit(struct battery_instance* instance) {
  struct battery_info* info = &instance->info;
  if (info->mode == ACPI_MODE_CHARGE)
    return 'A';
  else if (info->mode == ACPI_MODE_ENERGY)
    return 'W';
  else
    return '?';
}

static int read_uevent_fd(int fd, char* buffer, size_t buffer_size) {
  lseek(fd, 0, SEEK_SET);
  return read(fd, buffer, buffer_size);
}

static enum acpi_mode determine_acpi_mode(const char* buffer,
                                          size_t buffer_size) {
  if (memmem(buffer, buffer_size,
             PREFIX_CHARGE, STATIC_STRLEN(PREFIX_CHARGE)))
    return ACPI_MODE_CHARGE;
  else if (memmem(buffer, buffer_size,
                  PREFIX_ENERGY, STATIC_STRLEN(PREFIX_ENERGY)))
    return ACPI_MODE_ENERGY;
  else
    return ACPI_MODE_UNKNOWN;
}

static enum acpi_mode get_acpi_mode(int uevent_fd) {
  char buffer[1024];
  if (read_uevent_fd(uevent_fd, buffer, sizeof(buffer)) < 0)
    return ACPI_MODE_UNKNOWN;
  else
    return determine_acpi_mode(buffer, sizeof(buffer));
}

static void parse_uevent_line(char* s, const char** key, const char** value) {
  char *separator;

  ASSERT(key != NULL);
  ASSERT(value != NULL);

  if ((separator = strchr(s, '=')) == NULL)
    /* Line is not in the format KEY=VALUE */
    return;
  *separator = '\0';

  *key = s;
  *value = separator + 1;
}

static b8 parse_u64(u64* out, const char* value) {
  b8 changed;
  u64 parsed;
  char* endptr;

  endptr = NULL;
  changed = false;

  parsed = strtoul(value, &endptr, 0);

  if (endptr == NULL || (endptr != value && *endptr != '\0')) {
    module_error("could not parse %s as an unsigned integer", value);
    goto out;
  }

  changed = *out != parsed;
  if (changed)
    *out = parsed;

out:
  return changed;
}

static b8 parse_status(enum battery_status* out, const char* value) {
  b8 changed;
  enum battery_status parsed;

  if (!strcmp(value, "Charging"))
    parsed = BATTERY_STATUS_CHARGING;
  else if (!strcmp(value, "Discharging"))
    parsed = BATTERY_STATUS_DISCHARGING;
  else
    parsed = BATTERY_STATUS_UNKNOWN;

  changed = *out != parsed;
  if (changed)
    *out = parsed;

  return changed;
}

static b8 parse_consumption(struct battery_instance* instance,
                            const char* value) {
  b8 changed;
  size_t i, count;
  u64 avg, val;
  struct battery_samples* ring = &instance->consumption_ring;

  val = -1;
  if (!parse_u64(&val, value))
    return false;

  ring->values[ring->index++] = val;
  if (ring->index >= ARRAY_LENGTH(ring->values))
    ring->index = 0;

  avg = count = 0;
  for (i = 0; i < ARRAY_LENGTH(ring->values); ++i) {
    if ((val = ring->values[i]) != (u64)-1) {
      avg += val;
      ++count;
    }
  }
  avg /= count;

  changed = abs((int)(instance->info.consumption_avg - avg)) >= 10000;
  if (changed)
    instance->info.consumption_avg = avg;

  return changed;
}

/* This function returns true if an element of g_battery was changed,
 * false otherwise.
 */
static b8 handle_property(struct battery_instance* instance,
                          const char* key, const char* value) {
  b8 status_changed;
  struct battery_info* info = &instance->info;

  /* The list of all the possible properties can be found here:
   * https://github.com/torvalds/linux/blob/master/drivers/acpi/battery.c
   */

  /* Generic specific properties */
  if (!strcmp(key, "POWER_SUPPLY_CAPACITY"))
    return parse_u64(&info->percentage, value);
  else if (!strcmp(key, "POWER_SUPPLY_STATUS")) {
    status_changed = parse_status(&info->status, value);
    if (status_changed)
      reset_consumption_ring(instance);
    return status_changed;

  /* ACPI mode specific properties */
  } else if (info->mode == ACPI_MODE_CHARGE) {
    if (!strcmp(key, PREFIX_CHARGE "FULL"))
      return parse_u64(&info->capacity_max, value);
    else if (!strcmp(key, PREFIX_CHARGE "NOW"))
      return parse_u64(&info->capacity_now, value);
    else if (!strcmp(key, "POWER_SUPPLY_CURRENT_NOW"))
      return parse_consumption(instance, value);
  } else if (info->mode == ACPI_MODE_ENERGY) {
    if (!strcmp(key, PREFIX_ENERGY "FULL"))
      return parse_u64(&info->capacity_max, value);
    else if (!strcmp(key, PREFIX_ENERGY "NOW"))
      return parse_u64(&info->capacity_now, value);
    else if (!strcmp(key, "POWER_SUPPLY_POWER_NOW"))
      return parse_consumption(instance, value);
  }

  return false;
}

/* NOTE: This function modifies buffer */
static b8 parse_uevent(struct battery_instance* instance,
                       char* buffer, size_t buffer_size) {
  size_t l;
  char *s, *e;
  const char *key, *value;
  b8 changed;

  l = buffer_size;
  changed = false;
  for (s = buffer; (e = memchr(s, '\n', l)) != NULL; s = e + 1) {
    *e = '\0';
    l -= (e - s) + 1;
    parse_uevent_line(s, &key, &value);
    changed |= handle_property(instance, key, value);
  }

  return changed;
}

static void get_time_left(struct battery_instance* instance, i8* h, i8* m) {
  f64 value;
  struct battery_info* info = &instance->info;

  if (info->consumption_avg == 0) {
    *h = *m = 0;
    return;
  }

  /* Compute hours */
  value = (double)info->capacity_now / info->consumption_avg;
  *h = (i8)value;

  /* Compute minutes */
  value = (value - *h) * 60.0f;
  *m = (i8)value;
}

static const char* get_icon(struct battery_instance* instance) {
  size_t index;
  struct battery_icons* icons = &instance->icons;
  if (icons->length == 0)
    return "?";

  index = ((f32)instance->info.percentage / 100.0f) * icons->length;
  return icons->array[index];
}

static size_t time_formatter(char* buffer, size_t buffer_size, void* val) {
  u32 time_val = (u64)val;
  return snprintf(buffer, buffer_size,
                  "%2d:%02d", time_val >> 8, time_val & 0xFF);
}

static void get_formatted_text(char* buffer, size_t buffer_size,
                               const char* format,
                               struct battery_instance* instance,
                               i8 hours, i8 minutes, u64 percentage) {
  u64 time_val = (((u64)hours << 8) | (u64)minutes);
  b8 buffer_too_small =
    FORMAT(
      buffer, buffer_size, format,
      FORMAT_PARAM("charge-percentage", INTEGER, percentage),
      FORMAT_PARAM_CUSTOM("time", time_formatter, time_val),
      FORMAT_PARAM("icon", STRING, get_icon(instance))
    );
  ASSERT(!buffer_too_small);
}

static void generate_instance_text(struct battery_instance* instance,
                                   const char* format,
                                   char* buffer, size_t buffer_size) {
  i8 hours, minutes;
  get_time_left(instance, &hours, &minutes);
  get_formatted_text(buffer, buffer_size, format,
                     instance, hours, minutes, instance->info.percentage);
}

static size_t string_width(const char* str) {
  return font_string_width(str) + 8;
}

static size_t compute_width(struct battery_instance* instance) {
  char buffer[128];
  get_formatted_text(buffer, sizeof(buffer), instance->format,
                     instance, 99, 99, 100);
  return string_width(buffer);
}

static void battery_render(void* instance_ptr) {
  struct draw* draw;
  size_t required_width;
  char buffer[128];
  struct battery_instance* instance = instance_ptr;

  /* FIXME: Toggle between format and format_alt on click */
  generate_instance_text(instance, instance->format, buffer, sizeof(buffer));

  required_width = string_width(buffer);
  if (required_width != instance->zone->width)
    bar_resize_zone(instance->zone, required_width);

  draw_on_zone(instance->zone, draw) {
    draw_rect(draw,
              0, 0,
              draw_width(draw), draw_height(draw),
              instance->bg_color.as_u32);
    draw_string(draw, 4, 0, buffer, instance->fg_color.as_u32);
  }
}

static void trace_status(struct battery_instance* instance) {
  char unit;
  struct battery_info* info = &instance->info;

  unit = battery_unit(instance);
  module_trace("mode: %s", battery_mode(instance));
  module_trace("status: %s", battery_status(instance));
  module_trace("percentage: %lu%%", info->percentage);
  module_trace("consumption: %lu µ%c", info->consumption_avg, unit);
  module_trace("capacity_max: %lu µ%ch", info->capacity_max, unit);
  module_trace("capacity_now: %lu µ%ch", info->capacity_now, unit);
}

static void update_instance(struct battery_instance* instance) {
  ssize_t rc;
  char buffer[1024];

  rc = read_uevent_fd(instance->uevent_fd, buffer, sizeof(buffer));
  if (rc < 0) {
    module_error("could not read from uevent file '" BATTERY_PATH "': %m",
                 instance->name);
    return;
  }

  if (parse_uevent(instance, buffer, sizeof(buffer))) {
    trace_status(instance);
    battery_render(instance);
  }
}

static void update_info(void) {
  struct battery_instance* instance;

  list_for_each(instance, &g_instances, link)
    update_instance(instance);
}

static void get_battery_path(char* buffer, size_t buffer_size,
                             const char* battery_name) {
  size_t written = snprintf(buffer, buffer_size, BATTERY_PATH, battery_name);
  ASSERT(written < buffer_size);
}

static void parse_icons(void* data, size_t index, struct config_node* node) {
  struct battery_instance* instance = data;
  struct battery_icons* icons = &instance->icons;
  size_t array_index = icons->length++;

  /* FIXME: This is kinda inefficient */
  icons->array = realloc(icons->array, icons->length * sizeof(*icons->array));

  CONFIG_PARSE(node,
    CONFIG_PARAM(
      CONFIG_PARAM_NAME(CONFIG_PARAM_SELF),
      CONFIG_PARAM_TYPE(STRING),
      CONFIG_PARAM_STORE(icons->array[array_index])
    )
  );

  UNUSED(index);
}

static void free_icons(struct battery_instance* instance) {
  size_t i;
  struct battery_icons* icons = &instance->icons;

  for (i = 0; i < icons->length; ++i)
    free(icons->array[i]);
  free(icons->array);
}

static void battery_cleanup(void* instance_ptr) {
  struct battery_instance* instance = instance_ptr;

  if (g_task_id >= 0 && list_length(&g_instances) == 1)
    sched_task_delete(g_task_id);

  if (instance->uevent_fd > 0)
    close(instance->uevent_fd);
  if (instance->zone != NULL)
    bar_destroy_zone(&instance->zone);

  list_remove(&instance->link);
  free_icons(instance);
  free(instance->format_alt);
  free(instance->format);
  free(instance->name);
  free(instance);
}

static void* battery_init(struct module_init_data* init_data) {
  int rc;
  struct battery_instance* instance;
  enum acpi_mode acpi_mode;
  char uevent_path[sizeof(BATTERY_PATH) * 2];

  instance = zalloc(sizeof(*instance));
  ASSERT(instance != NULL);

  CONFIG_PARSE_WITH_DATA(instance, init_data->config,
    CONFIG_PARAM(
      CONFIG_PARAM_NAME("name"),
      CONFIG_PARAM_TYPE(STRING),
      CONFIG_PARAM_STORE(instance->name),
      CONFIG_PARAM_DEFAULT(BATTERY_DEFAULT_NAME)
    ),
    CONFIG_PARAM(
      CONFIG_PARAM_NAME("format"),
      CONFIG_PARAM_TYPE(STRING),
      CONFIG_PARAM_STORE(instance->format),
      CONFIG_PARAM_DEFAULT(BATTERY_DEFAULT_FORMAT)
    ),
    CONFIG_PARAM(
      CONFIG_PARAM_NAME("format-alt"),
      CONFIG_PARAM_TYPE(STRING),
      CONFIG_PARAM_STORE(instance->format_alt),
      CONFIG_PARAM_DEFAULT(BATTERY_DEFAULT_FORMAT_ALT)
    ),
    CONFIG_PARAM(
      CONFIG_PARAM_NAME("icons"),
      CONFIG_PARAM_TYPE(ARRAY),
      CONFIG_PARAM_STORE(parse_icons),
    )
  );

  get_battery_path(uevent_path, sizeof(uevent_path), instance->name);

  rc = instance->uevent_fd = open(uevent_path, O_RDONLY);
  if (rc < 0) {
    module_error("could not open uevent file '%s': %m", uevent_path);
    goto fail;
  }

  acpi_mode = get_acpi_mode(instance->uevent_fd);
  if (acpi_mode == ACPI_MODE_UNKNOWN) {
    module_error("could not determine acpi mode");
    goto fail;
  }

  reset_consumption_ring(instance);
  instance->zone = bar_alloc_zone(init_data->position, compute_width(instance));
  instance->info.mode = acpi_mode;
  instance->fg_color = init_data->foreground_color;
  instance->bg_color = init_data->background_color;

  module_trace("detected acpi %s mode", battery_mode(instance));

  if (!list_is_initialized(&g_instances))
    list_init(&g_instances);
  list_insert(&g_instances, &instance->link);

  if (g_task_id < 0)
    g_task_id = sched_task_interval(update_info, 1000, true);
  else
    update_instance(instance);

  fill_consumption_ring(instance);

  return instance;

fail:
  battery_cleanup(instance);
  return NULL;
}

MODULE_CALLBACKS(.init = battery_init,
                 .render = battery_render,
                 .cleanup = battery_cleanup);
