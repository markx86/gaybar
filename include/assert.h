#ifndef ASSERT_H_
#define ASSERT_H_

#include <log.h>

/* extern strstr(..) function, to avoid including the entire string.h header */
extern char* strstr(const char* haystack, const char* needle);

#define _ROOT_ "src/"

#define ASSERT(x)                                              \
  do {                                                         \
    if (!(x)) {                                                \
      const char* s = strstr(__FILE__, _ROOT_);                \
      log_fatal("(%s:%d) assertion %s failed",                 \
                s == NULL ? __FILE__ : s + sizeof(_ROOT_) - 1, \
                __LINE__, #x);                                 \
    }                                                          \
  } while (0)

#endif
