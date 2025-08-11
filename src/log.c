#include <gaybar/log.h>
#include <gaybar/params.h>
#include <gaybar/types.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static FILE* g_log_file;
static b8 g_use_stderr;

static void set_log_level(void) {
  char *env, *endptr;
  if (!g_params.log_level) {
    env = param_env("LOGLEVEL");
    if (env)
      g_params.log_level = strtol(env, &endptr, 10);
  }
  /* Clamp log level */
  if (g_params.log_level < LOG_LEVEL_MIN)
    g_params.log_level = LOG_LEVEL_MIN;
  else if (g_params.log_level > LOG_LEVEL_MAX)
    g_params.log_level = LOG_LEVEL_MAX;
}

static FILE* open_log_file(const char* path) {
  FILE* file;
  if (path == NULL)
    return NULL;
  file = fopen(path, "a");
  if (file)
    return file;
  log_error("could not open log file at %s: %m", path);
  return NULL;
}

static void get_date_time_string(char* buf, size_t len) {
  struct tm tm;
  time_t t = time(NULL);
  localtime_r(&t, &tm);
  strftime(buf, len, "[%F %T]", &tm);
}

static void write_log_file_header(void) {
  char date_time[64];
  get_date_time_string(date_time, sizeof(date_time));
  fprintf(g_log_file,
          "%1$s ####################################\n"
          "%1$s ######## gaybar is starting ########\n"
          "%1$s ####################################\n",
          date_time);
}

void log_init(void) {
  set_log_level();
  {
    g_use_stderr = isatty(STDERR_FILENO);
    setvbuf(stderr, NULL, _IONBF, 0);
  }
  g_log_file = open_log_file(g_params.log_file);
  if (g_log_file)
    write_log_file_header();
}

void log_cleanup(void) {
  if (g_log_file)
    fclose(g_log_file);
}

/* Generate log preamble: '[YYYY-MM-DD hh:mm:ss] [XXXXX]' */
static size_t gen_preamble(enum log_level level, char* buf, size_t len) {
  static const char* type[] = {
    [LOG_TRACE] = " [TRACE] ",
    [LOG_INFO]  = " [INFO]  ",
    [LOG_WARN]  = " [WARN]  ",
    [LOG_ERROR] = " [ERROR] ",
    [LOG_FATAL] = " [FATAL] "
  };
  get_date_time_string(buf, len);
  strncat(buf, type[level], len);
  return strlen(buf);
}

void _log(enum log_level level, const char *fmt, ...) {
  char pre[64];
  size_t pre_len;
  va_list ap_file, ap_stderr;

  if (level > g_params.log_level)
    return;

  va_start(ap_file, fmt);
  va_copy(ap_stderr, ap_file);

  pre_len = gen_preamble(level, pre, sizeof(pre));

  /* Print to stderr */
  if (g_use_stderr) {
    fwrite(pre, pre_len, 1, stderr);
    vfprintf(stderr, fmt, ap_stderr);
  }
  va_end(ap_stderr);

  /* Print to log file */
  if (g_log_file != NULL) {
    fwrite(pre, pre_len, 1, g_log_file);
    vfprintf(g_log_file, fmt, ap_file);
  }
  va_end(ap_file);
}
