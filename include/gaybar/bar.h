#ifndef BAR_H_
#define BAR_H_

#include <gaybar/types.h>
#include <gaybar/color.h>

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

struct zone {
  enum zone_position position;
  u32 width, height;
  u32* image_buffer;
};

int  bar_init(void);
void bar_loop(void);
void bar_cleanup(void);

u32               bar_get_thickness(void);
enum bar_position bar_get_position(void);
struct color      bar_get_background_color(void);
struct color      bar_get_foreground_color(void);

struct zone* bar_alloc_zone(enum zone_position position, u32 size);
void         bar_destroy_zone(struct zone** zonep);

void         zone_request_redraw(struct zone* zone);
b8           zone_should_redraw(struct zone* zone);

#endif
