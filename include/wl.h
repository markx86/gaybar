#ifndef WL_H_
#define WL_H_

#include <types.h>

enum bar_position;
struct zone;

int  wl_init(void);
int  wl_should_close(void);
void wl_cleanup(void);
b8   wl_draw_begin(void);
void wl_draw_end(void);
void wl_draw_zone(struct zone* zone, u32 offset, u32 position_width);
void wl_clear(u32 color);

#endif
