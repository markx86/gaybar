#ifndef PARAMS_H_
#define PARAMS_H_

#include <gaybar/log.h>

struct params {
  enum log_level log_level;
  char* log_file;
};

#define param_env(name) \
  getenv("GB_" name)

extern struct params g_params;

#endif
