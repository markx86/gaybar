#ifndef LOG_H_
#define LOG_H_

/* extern abort(..) function, to avoid including the entire stdlib.h header */
extern __attribute__((noreturn)) void abort();
/* extern strstr(..) function, to avoid including the entire string.h header */
extern char* strstr(const char* haystack, const char* needle);

enum log_level {
  LOG_FATAL     = 0,
  LOG_ERROR     = 1,
  LOG_WARN      = 2,
  LOG_INFO      = 3,
  LOG_TRACE     = 4,
};

#define LOG_LEVEL_MIN LOG_WARN
#define LOG_LEVEL_MAX LOG_TRACE

void log_init(void);

void _log(enum log_level level, const char* fmt, ...);

#define log_fatal(fmt, ...)                    \
  do {                                         \
    _log(LOG_FATAL, fmt "\n", ##__VA_ARGS__);  \
    abort();                                   \
  } while (0)
#define log_error(fmt, ...) \
  _log(LOG_ERROR, fmt "\n", ##__VA_ARGS__)
#define log_warn(fmt, ...) \
  _log(LOG_WARN, fmt "\n", ##__VA_ARGS__)
#define log_info(fmt, ...) \
  _log(LOG_INFO, fmt "\n", ##__VA_ARGS__)
#define log_trace(fmt, ...) \
  _log(LOG_TRACE, fmt "\n", ##__VA_ARGS__)

#define _ROOT_ "src/"

#define assert(x)                                              \
  do {                                                         \
    if (!(x)) {                                                \
      const char* s = strstr(__FILE__, _ROOT_);                \
      log_fatal("(%s:%d) assertion %s failed",                 \
                s == NULL ? __FILE__ : s + sizeof(_ROOT_) - 1, \
                __LINE__, #x);                                 \
    }                                                          \
  } while (0)

#endif
