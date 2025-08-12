#include <gaybar/draw.h>
#include <gaybar/font.h>
#include <gaybar/util.h>
#include <gaybar/bar.h>
#include <gaybar/assert.h>

#include <stdlib.h>

struct draw {
  struct zone* zone;
  b8 dirty;
};

static inline void mark_dirty(struct draw* draw) {
  if (!draw->dirty)
    draw->dirty = true;
}

struct draw* _draw_start(struct zone** zonep) {
  struct draw* draw = zalloc(sizeof(*draw));

  ASSERT(draw != NULL);
  ASSERT(zonep != NULL);
  ASSERT(*zonep != NULL);

  /* Take ownership of the zone */
  draw->zone = *zonep;
  draw->dirty = false;
  *zonep = NULL;

  return draw;
}

struct zone* _draw_end(struct draw** drawp) {
  struct zone* zone;
  struct draw* draw;

  ASSERT(drawp != NULL);
  ASSERT(*drawp != NULL);

  draw = *drawp;
  zone = draw->zone;

  if (draw->dirty)
    zone_request_redraw(zone);

  free(draw);
  *drawp = NULL;

  return zone;
}

void draw_rect(struct draw* draw, u32 x, u32 y, u32 w, u32 h, u32 color) {
  u32 width, height, *c;
  u32 sx, sy, ex, ey;

  ASSERT(draw != NULL);
  ASSERT(draw->zone != NULL);

  mark_dirty(draw);

  width = draw->zone->width;
  height = draw->zone->height;
  c = draw->zone->image_buffer;

  sx = min(x, width);
  sy = min(y, height);
  ex = min(sx + w, width);
  ey = min(sy + h, height);

  for (y = sy; y < ey; ++y) {
    for (x = sx; x < ex; ++x)
      c[x + y * width] = color;
  }
}

void draw_icon(struct draw* draw, u32 x, u32 y, u32 w, u32 h, u32* icon) {
  u32 width, height, *c;
  u32 sx, sy, ex, ey, ix, iy;

  ASSERT(icon != NULL);
  ASSERT(draw != NULL);
  ASSERT(draw->zone != NULL);

  mark_dirty(draw);

  width = draw->zone->width;
  height = draw->zone->height;
  c = draw->zone->image_buffer;

  sx = min(x, width);
  sy = min(y, height);
  ex = min(sx + w, width);
  ey = min(sy + h, height);

  for (iy = 0, y = sy; y < ey; ++iy, ++y) {
    for (ix = 0, x = sx; x < ex; ++ix, ++x)
      c[x + y * width] = icon[ix + iy * w];
  }
}

void draw_string(struct draw* draw, u32 x, u32 y,
                 const char* string, u32 color) {
  struct zone* zone;
  u32* buffer;
  size_t buffer_width, buffer_height, buffer_stride_in_pixels;

  ASSERT(string != NULL);
  ASSERT(draw != NULL);
  ASSERT(draw->zone != NULL);

  mark_dirty(draw);

  zone = draw->zone;

  if (x >= zone->width || y >= zone->height) {
    log_warn("drawing out of bounds (x=%zu, y=%zu) on zone (w=%zu, h=%zu)",
             x, y, zone->width, zone->height);
    return;
  }

  buffer_stride_in_pixels = zone->width;
  buffer_width = zone->width - x;
  buffer_height = zone->height - y;
  buffer = &zone->image_buffer[x + y * buffer_stride_in_pixels];
  font_string_render(string, false, color, buffer,
                     buffer_width, buffer_height, buffer_stride_in_pixels);
}
