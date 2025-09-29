#include <gaybar/module.h>
#include <gaybar/bar.h>
#include <gaybar/util.h>
#include <gaybar/draw.h>
#include <gaybar/sched.h>
#include <gaybar/font.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BATTERY_NAME "BAT1"
#define BATTERY_PATH ("/sys/class/power_supply/" BATTERY_NAME "/uevent")

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

struct battery_instance {
  struct list link;
  int uevent_fd;
  struct battery_info info;
  struct battery_samples consumption_ring;
  struct zone* zone;
};

static i64 g_task_id = -1;
static struct list g_instances = LIST_UNINITIALIZED;

MODULE("battery", "markx86", "Display battery information.");

static void reset_consumption_ring(struct battery_instance* instance) {
  struct battery_samples* ring = &instance->consumption_ring;
  ring->index = 0;
  memset(ring->values, 0xFF, sizeof(ring->values));
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

static int read_uevent(struct battery_instance* instance,
                       char* buffer, size_t buffer_size) {
  int rc;

  lseek(instance->uevent_fd, 0, SEEK_SET);
  rc = read(instance->uevent_fd, buffer, buffer_size);
  if (rc < 0)
    module_error("could not read from uevent file '%s': %m", BATTERY_PATH);

  return rc;
}

static enum acpi_mode determine_acpi_mode(const char* buffer,
                                          size_t buffer_size) {
  if (!memmem(buffer, buffer_size,
             PREFIX_CHARGE, STATIC_STRLEN(PREFIX_CHARGE)))
    return ACPI_MODE_CHARGE;
  else if (!memmem(buffer, buffer_size,
                  PREFIX_ENERGY, STATIC_STRLEN(PREFIX_ENERGY)))
    return ACPI_MODE_ENERGY;
  else
    return ACPI_MODE_UNKNOWN;
}

static int get_acpi_mode(struct battery_instance* instance) {
  char buffer[1024];
  instance->info.mode = determine_acpi_mode(buffer, sizeof(buffer));
  return -(instance->info.mode == ACPI_MODE_UNKNOWN);
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

  changed = instance->info.consumption_avg != avg;
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

static void get_formatted_text(char* buffer, size_t buffer_size,
                               const char* battery_name,
                               i8 hours, i8 minutes, u64 percentage) {
  size_t written;
  written = snprintf(buffer, buffer_size,
                     "%s: %02hhd:%02hhd left\nLVL: %lu%%",
                     battery_name, hours, minutes, percentage);
  ASSERT(written < buffer_size);
}

static void generate_instance_text(struct battery_instance* instance,
                                   char* buffer, size_t buffer_size) {
  i8 hours, minutes;
  get_time_left(instance, &hours, &minutes);
  get_formatted_text(buffer, buffer_size,
                     BATTERY_NAME, hours, minutes, instance->info.percentage);
}

static size_t compute_width(void) {
  char buffer[128];
  get_formatted_text(buffer, sizeof(buffer), BATTERY_NAME, 99, 99, 100);
  return font_string_width(buffer) + 8;
}

static void battery_render(void* instance_ptr) {
  struct draw* draw;
  char buffer[128];
  struct battery_instance* instance = instance_ptr;

  generate_instance_text(instance, buffer, sizeof(buffer));

  draw_on_zone(instance->zone, draw) {
    draw_rect(draw,
              0, 0,
              draw_width(draw), draw_height(draw),
              COLOR_AS_U32(255, 255, 0));
    draw_string(draw, 4, 0, buffer, COLOR_AS_U32(128, 0, 255));
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

  rc = read_uevent(instance, buffer, sizeof(buffer));
  if (rc < 0)
    return;

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

static void* battery_init(enum zone_position position,
                          struct config_node* config) {
  int rc;
  struct battery_instance* instance;

  UNUSED(config);

  instance = zalloc(sizeof(*instance));
  ASSERT(instance != NULL);

  reset_consumption_ring(instance);

  rc = instance->uevent_fd = open(BATTERY_PATH, O_RDONLY);
  if (rc < 0) {
    module_error("could not open uevent file '%s': %m", BATTERY_PATH);
    return NULL;
  }

  rc = get_acpi_mode(instance);
  if (rc < 0) {
    module_error("could not determine acpi mode");
    return NULL;
  }
  module_trace("detected acpi %s mode", battery_mode(instance));

  instance->zone = bar_alloc_zone(position, compute_width());

  if (g_task_id < 0)
    g_task_id = sched_task_interval(update_info, 1000, true);
  else
    update_instance(instance);

  if (!list_is_initialized(&g_instances))
    list_init(&g_instances);
  list_insert(&g_instances, &instance->link);

  return instance;
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
  free(instance);
}

MODULE_CALLBACKS(.init = battery_init,
                 .render = battery_render,
                 .cleanup = battery_cleanup);
