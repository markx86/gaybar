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

static i64 g_task_id = -1;
static int g_uevent_fd = -1;
static struct battery_info g_battery = {0};
static struct zone* g_zone = NULL;
static struct {
  size_t index;
  u64 values[AVG_SAMPLES];
} g_consumption_ring;

MODULE("battery", "markx86", "Display battery information.");

static void reset_consumption_ring(void) {
  g_consumption_ring.index = 0;
  memset(g_consumption_ring.values, 0xFF, sizeof(g_consumption_ring.values));
}

static const char* battery_mode(void) {
  if (g_battery.mode == ACPI_MODE_CHARGE)
    return "charge";
  else if (g_battery.mode == ACPI_MODE_ENERGY)
    return "energy";
  else
    return "(unknown)";
}

static const char* battery_status(void) {
  if (g_battery.status == BATTERY_STATUS_CHARGING)
    return "charging";
  else if (g_battery.status == BATTERY_STATUS_DISCHARGING)
    return "discharging";
  else
    return "(unknown)";
}

static char battery_unit(void) {
  if (g_battery.mode == ACPI_MODE_CHARGE)
    return 'A';
  else if (g_battery.mode == ACPI_MODE_ENERGY)
    return 'W';
  else
    return '?';
}

static int read_uevent(char* buffer, size_t buffer_size) {
  int rc;

  lseek(g_uevent_fd, 0, SEEK_SET);
  rc = read(g_uevent_fd, buffer, buffer_size);
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

static int get_acpi_mode(void) {
  char buffer[1024];
  g_battery.mode = determine_acpi_mode(buffer, sizeof(buffer));
  return -(g_battery.mode == ACPI_MODE_UNKNOWN);
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

static b8 parse_consumption(u64* out, const char* value) {
  b8 changed;
  size_t i, count;
  u64 avg, val = -1;

  if (!parse_u64(&val, value))
    return false;

  g_consumption_ring.values[g_consumption_ring.index++] = val;
  if (g_consumption_ring.index >= ARRAY_LENGTH(g_consumption_ring.values))
    g_consumption_ring.index = 0;

  avg = count = 0;
  for (i = 0; i < ARRAY_LENGTH(g_consumption_ring.values); ++i) {
    if ((val = g_consumption_ring.values[i]) != (u64)-1) {
      avg += val;
      ++count;
    }
  }
  avg /= count;

  changed = *out != avg;
  if (changed)
    *out = avg;

  return changed;
}

/* This function returns true if an element of g_battery was changed,
 * false otherwise.
 */
static b8 handle_property(const char* key, const char* value) {
  b8 status_changed;

  /* The list of all the possible properties can be found here:
   * https://github.com/torvalds/linux/blob/master/drivers/acpi/battery.c
   */

  /* Generic specific properties */
  if (!strcmp(key, "POWER_SUPPLY_CAPACITY"))
    return parse_u64(&g_battery.percentage, value);
  else if (!strcmp(key, "POWER_SUPPLY_STATUS")) {
    status_changed = parse_status(&g_battery.status, value);
    if (status_changed)
      reset_consumption_ring();
    return status_changed;

  /* ACPI mode specific properties */
  } else if (g_battery.mode == ACPI_MODE_CHARGE) {
    if (!strcmp(key, PREFIX_CHARGE "FULL"))
      return parse_u64(&g_battery.capacity_max, value);
    else if (!strcmp(key, PREFIX_CHARGE "NOW"))
      return parse_u64(&g_battery.capacity_now, value);
    else if (!strcmp(key, "POWER_SUPPLY_CURRENT_NOW"))
      return parse_consumption(&g_battery.consumption_avg, value);
  } else if (g_battery.mode == ACPI_MODE_ENERGY) {
    if (!strcmp(key, PREFIX_ENERGY "FULL"))
      return parse_u64(&g_battery.capacity_max, value);
    else if (!strcmp(key, PREFIX_ENERGY "NOW"))
      return parse_u64(&g_battery.capacity_now, value);
    else if (!strcmp(key, "POWER_SUPPLY_POWER_NOW"))
      return parse_consumption(&g_battery.consumption_avg, value);
  }

  return false;
}

/* NOTE: This function modifies buffer */
static b8 parse_uevent(char* buffer, size_t buffer_size) {
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
    changed |= handle_property(key, value);
  }

  return changed;
}

static void get_time_left(i8* h, i8* m) {
  f64 value;

  if (g_battery.consumption_avg == 0) {
    *h = *m = 0;
    return;
  }

  /* Compute hours */
  value = (double)g_battery.capacity_now / g_battery.consumption_avg;
  *h = (i8)value;

  /* Compute minutes */
  value = (value - *h) * 60.0f;
  *m = (i8)value;
}

static void generate_format_text(char* buffer, size_t buffer_size,
                                 const char* battery_name,
                                 i8 hours, i8 minutes, u64 percentage) {
  snprintf(buffer, buffer_size,
           "%s: %02hhd:%02hhd left\nLVL: %lu%%",
           battery_name, hours, minutes, percentage);
}

static size_t compute_width(void) {
  char buffer[128];
  generate_format_text(buffer, sizeof(buffer), BATTERY_NAME, 99, 99, 100);
  return font_string_width(buffer) + 8;
}

static void battery_render(void) {
  struct draw* draw;
  i8 hours, minutes;
  char buffer[128];

  get_time_left(&hours, &minutes);
  generate_format_text(buffer, sizeof(buffer),
                       BATTERY_NAME, hours, minutes, g_battery.percentage);

  draw_on_zone(g_zone, draw) {
    draw_rect(draw,
              0, 0,
              draw_width(draw), draw_height(draw),
              COLOR_AS_U32(255, 255, 0));
    draw_string(draw, 4, 0, buffer, COLOR_AS_U32(128, 0, 255));
  }
}

static void trace_status(void) {
  module_trace("mode: %s", battery_mode());
  module_trace("status: %s", battery_status());
  module_trace("percentage: %lu%%", g_battery.percentage);
  module_trace("consumption: %lu µ%c",
               g_battery.consumption_avg, battery_unit());
  module_trace("capacity_max: %lu µ%ch",
               g_battery.capacity_max, battery_unit());
  module_trace("capacity_now: %lu µ%ch",
               g_battery.capacity_now, battery_unit());

}

static void update_info(void) {
  ssize_t rc;
  char buffer[1024];

  rc = read_uevent(buffer, sizeof(buffer));
  if (rc < 0)
    return;

  if (parse_uevent(buffer, sizeof(buffer))) {
    trace_status();
    battery_render();
  }
}

static int battery_init(void) {
  int rc;

  reset_consumption_ring();

  rc = g_uevent_fd = open(BATTERY_PATH, O_RDONLY);
  if (rc < 0) {
    module_error("could not open uevent file '%s': %m", BATTERY_PATH);
    goto fail;
  }

  rc = get_acpi_mode();
  if (rc < 0)
    module_error("could not determine acpi mode");
  else
    module_trace("detected acpi %s mode", battery_mode());

  g_zone = bar_alloc_zone(ZONE_POSITION_RIGHT, compute_width());

  g_task_id = sched_task_interval(update_info, 1000, true);
fail:
  return rc;
}

static void battery_cleanup(void) {
  if (g_task_id >= 0)
    sched_task_delete(g_task_id);
  if (g_uevent_fd > 0)
    close(g_uevent_fd);
  if (g_zone != NULL)
    bar_destroy_zone(&g_zone);
}

MODULE_CALLBACKS(.init = battery_init,
                 .render = battery_render,
                 .cleanup = battery_cleanup);
