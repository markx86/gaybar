#ifndef WL_H_
#define WL_H_

#include <types.h>

enum bar_position;

int  wl_init(enum bar_position position, u32 size);
int  wl_should_close(void);
void wl_cleanup(void);
b8   wl_draw_begin(void);
void wl_draw_end(void);
void wl_draw_element(f32 x, f32 y, u32 width, u32 height, void* data);
void wl_clear(u32 color);

#endif
