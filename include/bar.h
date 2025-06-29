#ifndef BAR_H_
#define BAR_H_

#include <types.h>

enum bar_position {
  BAR_POSITION_TOP,
  BAR_POSITION_BOTTOM,
  BAR_POSITION_MAX
};

struct color {
  union {
    struct {
      u8 b;
      u8 g;
      u8 r;
      u8 a;
    };
    u32 as_u32;
  };
};

#define COLOR(rr, gg, bb) \
  ((struct color) { .a = 0xFF, .r = rr, .g = gg, .b = bb })
#define COLORU32(rr, gg, bb) \
  (COLOR(rr, gg, bb).as_u32)

int bar_init(enum bar_position position, u32 size);
int bar_loop(void);
void bar_cleanup(void);

#endif
