#include <gaybar/log.h>
#include <gaybar/params.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static FILE* log_file;
static int use_stderr;

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

void log_init(void) {
  set_log_level();
  {
    use_stderr = isatty(STDERR_FILENO);
    setvbuf(stderr, NULL, _IONBF, 0);
  }
  log_file = open_log_file(g_params.log_file);
}

/* Generate log preamble: '[YYYY-MM-DD hh:mm:ss] [XXXXX]' */
static size_t gen_preamble(enum log_level level, char* buf, size_t len) {
  const char* type[] = {
    [LOG_TRACE] = "[TRACE] ",
    [LOG_INFO]  = "[INFO]  ",
    [LOG_WARN]  = "[WARN]  ",
    [LOG_ERROR] = "[ERROR] ",
    [LOG_FATAL] = "[FATAL] "
  };
  struct tm tm;
  time_t t = time(NULL);
  localtime_r(&t, &tm);
  strftime(buf, len, "[%F %T] ", &tm);
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
  if (use_stderr) {
    fwrite(pre, pre_len, 1, stderr);
    vfprintf(stderr, fmt, ap_stderr);
  }
  va_end(ap_stderr);

  /* Print to log file */
  if (log_file != NULL) {
    fwrite(pre, pre_len, 1, log_file);
    vfprintf(log_file, fmt, ap_file);
  }
  va_end(ap_file);
}
