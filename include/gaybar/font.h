#ifndef FONT_H_
#define FONT_H_

#include <gaybar/types.h>

int  font_init(void);
void font_cleanup(void);

void font_render_string(const char* string, b8 wrap, u32 color, u32* buffer,
                        size_t buffer_width, size_t buffer_height,
                        size_t buffer_stride_in_pixels);

#endif
