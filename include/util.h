#ifndef UTIL_H_
#define UTIL_H_

#include <types.h>

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

#endif
