#ifndef COMPILER_H_
#define COMPILER_H_

#define STATICASSERT(x) \
  _Static_assert(x, "static assertion failed: " #x)
#define UNUSED(x) ((void)(x))

#endif
