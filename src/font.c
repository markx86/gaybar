#include <gaybar/log.h>
#include <gaybar/assert.h>
#include <gaybar/wl.h>
#include <gaybar/bar.h>
#include <gaybar/list.h>
#include <gaybar/util.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define DEFAULT_FONT \
  "/usr/share/fonts/adobe-source-code-pro-fonts/SourceCodePro-Regular.otf"
#define DEFAULT_FONT_SIZE 24

struct vec2u32 {
  u32 x, y;
};

struct rendered_glyph {
  b8 owns_bitmap;
  u32 width, height;
  struct vec2u32 offset, advance;
  u64 bitmap_pitch;
  unsigned char* bitmap;
};

struct font {
  const char* file_path;
  size_t size_in_pixels;
  FT_Face face;
};

static struct font g_font;
static FT_Library g_library;

static const char* ft_strerror(FT_Error error) {
  /* https://stackoverflow.com/questions/31161284 */
  #undef FTERRORS_H_
  #define FT_ERRORDEF(e, v, s)  case e: return s;
  #define FT_ERROR_START_LIST   switch (error) {
  #define FT_ERROR_END_LIST     }
  #include FT_ERRORS_H
  return "(Unknown error)";
}

static b8 render_glyph(u64 char_code, struct rendered_glyph* res, b8 copy) {
  size_t y;
  FT_GlyphSlot glyph;
  FT_Error error;

  error = FT_Load_Char(g_font.face, char_code, FT_LOAD_RENDER);
  if (error) {
    log_warn("could not load glyph for character code %#lx: %s",
             char_code, ft_strerror(error));
    return false;
  }

  glyph = g_font.face->glyph;

  ASSERT(glyph->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY);
  ASSERT(g_font.size_in_pixels > (size_t)abs(glyph->bitmap_top));

  res->width = glyph->bitmap.width;
  res->height = glyph->bitmap.rows;
  res->advance.x = glyph->advance.x;
  res->advance.y = glyph->advance.y;
  res->offset.x = glyph->bitmap_left;
  res->offset.y = g_font.size_in_pixels - glyph->bitmap_top;
  res->owns_bitmap = copy;
  res->bitmap_pitch = glyph->bitmap.pitch;
  if (copy) {
    res->bitmap = malloc(res->width * res->height);
    res->bitmap_pitch = res->width;
    for (y = 0; y < res->height; ++y) {
      memcpy(res->bitmap + y * res->bitmap_pitch,
             glyph->bitmap.buffer + y * glyph->bitmap.pitch,
             glyph->bitmap.pitch);
    }
  } else
    /* NOTE: If copy is not true, res->bitmap is only valid until the next
     *       call to this function.
     */
    res->bitmap = glyph->bitmap.buffer;

  return true;
}

static struct vec2u32 render_colored_glyph(u64 char_code,
                                           u32 color, u32* buffer,
                                           size_t width, size_t height,
                                           size_t stride_in_px) {
  f32 alpha, inv_alpha;
  u64 x, y, buffer_cursor, bitmap_cursor;
  struct color c1, c2;
  struct rendered_glyph glyph;

  if (!render_glyph(char_code, &glyph, false))
    /* that glyph does not exist, skip it */
    return (struct vec2u32) { .x = g_font.size_in_pixels << 6, .y = 0 };

  buffer_cursor = glyph.offset.y * stride_in_px + glyph.offset.x;
  bitmap_cursor = 0;
  c1.as_u32 = color;

  for (y = 0; y < glyph.height; ++y) {
    if (y >= height)
      break;

    for (x = 0; x < glyph.width; ++x) {
      if (x >= width)
        break;

      /* get grayscale color and map it to [0.0f, 1.0f] */
      alpha = (f32)glyph.bitmap[bitmap_cursor + x] / 255.0f;
      inv_alpha = 1.0f - alpha;

      /* mix the font color and the background color by the alpha channel */
      c2.as_u32 = buffer[buffer_cursor + x];
      c2.r = (c2.r * inv_alpha) + (c1.r * alpha);
      c2.g = (c2.g * inv_alpha) + (c1.g * alpha);
      c2.b = (c2.b * inv_alpha) + (c1.b * alpha);
      buffer[buffer_cursor + x] = c2.as_u32;
    }

    /* advance cursors */
    bitmap_cursor += glyph.bitmap_pitch;
    buffer_cursor += stride_in_px;
  }

  return glyph.advance;
}

static u64 utf8_next_char(const char** s) {
  /* https://it.wikipedia.org/wiki/UTF-8 */
  u8 c;
  size_t consume;
  u64 code;

  ASSERT(s != NULL);
  ASSERT(*s != NULL);

  code = *((*s)++);

  /* ASCII character */
  if ((code & 0b10000000) == 0)
    return code;
  else
    consume = 1;

  /* UTF character between 0x000080-0x0007FF */
  if ((code >> 5) == 0b110) {
    code &= 0b11111;
    goto consume_bytes;
  } else
    ++consume;

  /* UTF character between 0x000800-0x00FFFF */
  if ((code >> 4) == 0b1110) {
    code &= 0b1111;
    goto consume_bytes;
  } else
    ++consume;

  if ((code >> 3) == 0b11110)
    code &= 0b111;
  else
    log_fatal("invalid character %#04lx", code);

consume_bytes:
  for (; consume > 0; --consume) {
    code <<= 6;
    c = (*((*s)++));
    ASSERT((c >> 6) == 0b10);
    code |= c & 0b00111111;
  }

  return code;
}

void font_render_string(const char* string, b8 wrap, u32 color, u32* buffer,
                        size_t buffer_width, size_t buffer_height,
                        size_t buffer_stride_in_pixels) {
  u64 char_code;
  struct vec2u32 advance;
  size_t x64ths, y64ths, cursor;
  const char* s = string;

  /* We assume the string is encoded in UTF-8 */

  /* x64ths and y64ths represent the x and y coordinates in 1/64th of a pixel,
   * hence why all the bitshifts by 6. Remember that x << 6 == x * 64 and that
   * x >> 6 == x / 64.
   *
   * FIXME: Maybe we should cleanup this bitshifts a little bit.
   */
  x64ths = 0; y64ths = 0;
  cursor = 0;
  while (*s) {
    char_code = utf8_next_char(&s);

    advance = render_colored_glyph(char_code, color,
                                   &buffer[cursor + (x64ths >> 6)],
                                   buffer_width - (x64ths >> 6),
                                   buffer_height - (y64ths >> 6),
                                   buffer_stride_in_pixels);

    /* increment coordinates */
    x64ths += advance.x;
    if (x64ths >= (buffer_width << 6)) {
      if (!wrap)
        return;
      x64ths = 0;
      y64ths += advance.y == 0 ? (g_font.size_in_pixels << 6) : advance.y;
      if (y64ths >= (buffer_height << 6))
        return;
      else
        cursor = y64ths * buffer_stride_in_pixels;
    }
  }
}

int font_init(void) {
  FT_Error error;

  g_font.file_path = DEFAULT_FONT;
  g_font.size_in_pixels = DEFAULT_FONT_SIZE;

  error = FT_Init_FreeType(&g_library);
  if (error) {
    log_error("could not initialize freetype: %s", ft_strerror(error));
    return -1;
  }

  error = FT_New_Face(g_library, g_font.file_path, 0, &g_font.face);
  if (error) {
    log_error("could not load font '%s': %s", DEFAULT_FONT, ft_strerror(error));
    return -1;
  }

  error = FT_Set_Pixel_Sizes(g_font.face, 0, g_font.size_in_pixels);
  if (error) {
    log_error("could not set character size: %s", ft_strerror(error));
    return -1;
  }

  error = FT_Select_Charmap(g_font.face, FT_ENCODING_UNICODE);
  if (error) {
    log_error("could not select unicode charmap: %s", ft_strerror(error));
    return -1;
  }

  return 0;
}

void font_cleanup(void) {
  FT_Done_Face(g_font.face);
  FT_Done_FreeType(g_library);
}
