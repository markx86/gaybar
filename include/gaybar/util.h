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

static inline i32 min(i32 x, i32 y) {
  return x < y ? x : y;
}

static inline i32 max(i32 x, i32 y) {
  return x > y ? x : y;
}

static inline i32 clamp(i32 x, i32 m, i32 M) {
  return min(max(m, x), M);
}

static inline i64 signi(i64 x) {
  return x == 0 ? 0 : x > 0 ? +1 : -1;
}

static inline void monotonic_time(struct timespec* tm) {
  ASSERT(clock_gettime(CLOCK_MONOTONIC, tm) == 0);
}

#define ARRAYLENGTH(x)  (sizeof(x) / sizeof(*x))
#define STATICSTRLEN(x) (sizeof(x) - 1)

#endif
