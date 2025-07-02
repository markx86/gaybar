#ifndef PARAMS_H_
#define PARAMS_H_

#include "log.h"

struct params {
  enum log_level log_level;
  const char* log_file;
};

#define param_env(name) \
  getenv("GB_" name)

extern struct params g_params;

#endif
