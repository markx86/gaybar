#ifndef BAR_H_
#define BAR_H_

#include <types.h>

enum bar_position {
  BAR_POSITION_TOP,
  BAR_POSITION_BOTTOM,
  BAR_POSITION_MAX
};

enum zone_position {
  ZONE_POSITION_LEFT,
  ZONE_POSITION_CENTER,
  ZONE_POSITION_RIGHT,
  ZONE_POSITION_MAX
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

struct zone {
  enum zone_position position;
  u32 width, height;
  u32* image_buffer;
};

#define COLOR(rr, gg, bb) \
  ((struct color) { .a = 0xFF, .r = rr, .g = gg, .b = bb })
#define COLORU32(rr, gg, bb) \
  (COLOR(rr, gg, bb).as_u32)

int  bar_init(enum bar_position position, u32 thickness);
int  bar_loop(void);
void bar_cleanup(void);

enum bar_position bar_get_position(void);
u32               bar_get_thickness(void);

struct zone* bar_alloc_zone(enum zone_position position, u32 size);
void         bar_destroy_zone(struct zone** zonep);
void         bar_redraw_zone(struct zone* zone);
b8           bar_should_redraw_zone(struct zone* zone);

#endif
