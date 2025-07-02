#ifndef COMPILER_H_
#define COMPILER_H_

#define STATICASSERT(x) \
  _Static_assert(x, "static assertion failed: " #x)

#define OFFSETOF(T, m) __builtin_offsetof(T, m)
#define TYPEOF(x)      __typeof__(x)

#define CONTAINEROF(o, T, m) \
  ((T*)((unsigned long)(o) - OFFSETOF(T, m)))

#define UNUSED(x) ((void)(x))

#endif
