#ifndef COMPILER_H_
#define COMPILER_H_

#define STATIC_ASSERT(x) \
  _Static_assert(x, "static assertion failed: " #x)

#define OFFSET_OF(T, m) __builtin_offsetof(T, m)
#define TYPE_OF(x)      __typeof__(x)

#define CONTAINER_OF(o, T, m) \
  ((T*)((unsigned long)(o) - OFFSET_OF(T, m)))

#define UNUSED(x) ((void)(x))

#define CONSTRUCTOR __attribute__((constructor))

#endif
