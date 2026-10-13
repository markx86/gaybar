#ifndef ARENA_H_
#define ARENA_H_

#ifndef ARENA_ALIGNMENT
#define ARENA_ALIGNMENT 8
#endif

#include <gaybar/types.h>
#include <gaybar/util.h>

struct arena {
  u8* alloc;
  u8* base;
  u8* top;
  u8* end;
};

static inline void* arena_base(struct arena* arena) {
  return arena->base;
}

static inline size_t arena_size(struct arena* arena) {
  return arena->top - arena->base;
}

static inline void* arena_global_base(struct arena* arena) {
  return (void*)P2ALIGN_UP((u64)arena->alloc, ARENA_ALIGNMENT);
}

static inline size_t arena_total_size(struct arena* arena) {
  return arena->top - (u8*)arena_global_base(arena);
}

static inline void* arena_top(struct arena* arena) {
  return arena->top;
}

static inline b8 arena_init(struct arena* arena, size_t bytes) {
  arena->alloc = zalloc(bytes);

  if (arena->alloc == NULL)
    return false;

  arena->base = arena_global_base(arena);
  arena->end = arena->alloc + bytes;
  arena->top = arena->base;

  return true;
}

static inline void* arena_alloc(struct arena* arena, size_t bytes) {
  void* alloc;

  if (arena->top + bytes < arena->end)
    return NULL;

  alloc = arena->top;
  arena->top += P2ALIGN_UP(bytes, ARENA_ALIGNMENT);
  return alloc;
}

static inline void arena_reset(struct arena* arena) {
  arena->top = arena->base;
}

static inline void arena_reset_all(struct arena* arena) {
  arena->base = arena_global_base(arena);
  arena->top = arena->base;
}

static inline void arena_squash(struct arena* arena) {
  extern void* memmove(void* dest, const void* src, size_t n);

  size_t move_size;
  u8* g_base = arena_global_base(arena);

  if (arena->base > g_base) {
    move_size = arena->top - g_base;
    memmove(arena->base, g_base, move_size);
    arena->top = arena->base + move_size;
  }
}

static inline b8 arena_push(struct arena* arena) {
  u8** base_store = arena_alloc(arena, sizeof(arena->base));
  if (base_store == NULL)
    return false;

  *base_store = arena->base;
  arena->base = arena->top;

  return true;
}

static inline b8 arena_pop(struct arena* arena) {
  u8** base_store = (u8**)P2ALIGN_DOWN((u64)arena->base - sizeof(arena->base),
                                       ARENA_ALIGNMENT);
  /* Check if the pointer is invalid.
   * Note that the previous base should be < the current base.
   */
  if (*base_store < arena->alloc || *base_store >= arena->base)
    return false;

  arena_reset(arena);
  arena->base = *base_store;
  arena->top = (u8*)base_store;

  return true;
}

static inline void arena_free(struct arena* arena) {
  extern void free(void* ptr);

  free(arena->alloc);
  *arena = (struct arena) {0};
}

#endif
