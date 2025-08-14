#ifndef DRAW_H_
#define DRAW_H_

#include <gaybar/types.h>

struct draw;
struct zone;

struct draw* _draw_start(struct zone** zonep);
struct zone* _draw_end(struct draw** drawp);

#define draw_on_zone(zone, draw) \
  for (draw = _draw_start(&zone); zone == NULL; zone = _draw_end(&draw))

void draw_rect(struct draw* draw, u32 x, u32 y, u32 w, u32 h, u32 color);
void draw_icon(struct draw* draw, u32 x, u32 y, u32 w, u32 h, u32* icon);
void draw_string(struct draw* draw, u32 x, u32 y,
                 const char* string, u32 color);

u32 draw_width(struct draw* draw);
u32 draw_height(struct draw* draw);

#endif
