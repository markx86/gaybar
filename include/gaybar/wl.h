#ifndef WL_H_
#define WL_H_

#include <gaybar/types.h>

enum bar_position;
enum zone_position;
struct zone;

int  wl_init(void);
int  wl_should_close(void);
void wl_cleanup(void);
b8   wl_draw_begin(void);
void wl_draw_end(void);
void wl_draw_zone(struct zone* zone, u32 position_width, u32 offset);
void wl_clear(u32 color);
void wl_clear_horizontal_region(enum zone_position position, u32 position_width,
                                u32 position_offset, u32 width, u32 color);

#endif
