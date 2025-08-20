#ifndef UTIL_H_
#define UTIL_H_

#include <gaybar/types.h>
#include <gaybar/assert.h>

#include <time.h>

static inline void* zalloc(size_t size) {
  /* TIL extern works inside of function bodies :^) */
  extern void* calloc(size_t nmemb, size_t size);
  return calloc(1, size);
}

static inline i64 min(i64 x, i64 y) {
  return x < y ? x : y;
}

static inline i64 max(i64 x, i64 y) {
  return x > y ? x : y;
}

static inline i64 clamp(i64 x, i64 m, i64 M) {
  return min(max(m, x), M);
}

static inline i64 signi(i64 x) {
  return x == 0 ? 0 : x > 0 ? +1 : -1;
}

static inline void monotonic_time(struct timespec* tm) {
  ASSERT(clock_gettime(CLOCK_MONOTONIC, tm) == 0);
}

#define ARRAY_LENGTH(x)  (sizeof(x) / sizeof(*x))
#define STATIC_STRLEN(x) (sizeof(x) - 1)

#endif
