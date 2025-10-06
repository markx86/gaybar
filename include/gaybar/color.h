#ifndef COLOR_H_
#define COLOR_H_

#include <gaybar/types.h>

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
#define COLOR_AS_U32(rr, gg, bb) \
  (COLOR(rr, gg, bb).as_u32)

b8 color_from_hex(const char* s, struct color* color);

#endif
