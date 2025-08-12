#ifndef FONT_H_
#define FONT_H_

#include <gaybar/types.h>

int  font_init(void);
void font_cleanup(void);
void font_cache_clear(void);

void   font_set_size(size_t pixels);
size_t font_get_size(void);

size_t font_string_width(const char* string);
void   font_string_render(const char* string, b8 wrap, u32 color, u32* buffer,
                          size_t buffer_width, size_t buffer_height,
                          size_t buffer_stride_in_pixels);

#endif
