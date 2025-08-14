#include <ctype.h>
#include <gaybar/log.h>
#include <gaybar/assert.h>
#include <gaybar/wl.h>
#include <gaybar/bar.h>
#include <gaybar/list.h>
#include <gaybar/util.h>
#include <gaybar/compiler.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define DEFAULT_FONT \
  "/usr/share/fonts/adobe-source-code-pro-fonts/SourceCodePro-Regular.otf"
#define DEFAULT_FONT_SIZE 14

#define CACHE_ASCII_SIZE 128
#define CACHE_EXTRA_SIZE 256
#define CACHE_TOTAL_SIZE (CACHE_ASCII_SIZE + CACHE_EXTRA_SIZE)

struct vec2u32 {
  u32 x, y;
};

struct rendered_glyph {
  b8 owns_bitmap;
  u32 width, height;
  struct vec2u32 offset, advance;
  unsigned char* bitmap;
};

struct cached_glyph {
  struct rendered_glyph glyph;
  u32 char_code;
  u32 hits;
};
STATICASSERT(OFFSETOF(struct cached_glyph, glyph) == 0);

struct font {
  const char* file_path;
  size_t size_in_pixels;
  FT_Face face;
};

struct font_cache {
  union {
    struct {
      struct cached_glyph* ascii[CACHE_ASCII_SIZE];
      struct cached_glyph* extra[CACHE_EXTRA_SIZE];
    };
    struct cached_glyph* all_cached_glyphs[CACHE_TOTAL_SIZE];
  };
  u32 wants_slot[CACHE_EXTRA_SIZE];
};

static struct font g_font;
static struct font_cache g_font_cache;
static FT_Library g_library;

static const char* ft_strerror(FT_Error error) {
  /* https://stackoverflow.com/questions/31161284 */
  #undef FTERRORS_H_
  #define FT_ERRORDEF(e, v, s)  case e: return s;
  #define FT_ERROR_START_LIST   switch (error) {
  #define FT_ERROR_END_LIST     }
  #include FT_ERRORS_H
  return "(unknown error)";
}

static b8 render_glyph(u32 char_code, struct rendered_glyph* res, b8 copy) {
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
  if (copy) {
    res->bitmap = malloc(res->height * res->width);
    ASSERT(res->bitmap != NULL);
    for (y = 0; y < res->height; ++y)
      memcpy(res->bitmap + y * res->width,
             glyph->bitmap.buffer + y * glyph->bitmap.pitch,
             res->width);
  } else
    /* NOTE: If copy is not true, res->bitmap is only valid until the next
     *       call to this function.
     */
    res->bitmap = glyph->bitmap.buffer;

  return true;
}

static struct rendered_glyph* cache_glyph(u32 char_code,
                                          struct cached_glyph** slot) {
  struct rendered_glyph glyph;

  if (!render_glyph(char_code, &glyph, true))
    return NULL;

  *slot = malloc(sizeof(**slot));
  ASSERT(*slot != NULL);
  (*slot)->char_code = char_code;
  (*slot)->hits = 1;
  (*slot)->glyph = glyph;

  log_trace("caching glyph for char code %#lx", char_code);

  return &(*slot)->glyph;
}

static inline u8 get_char_code_hash(u32 char_code) {
  size_t i;
  u8 hash = 0;
  for (i = 0; i < 4; ++i) {
    hash ^= char_code & 0xFF;
    char_code >>= 8;
  }
  return hash;
}

/* FIXME: I've never made a cache so idk if this is any good, somebody should
 *        bechmark it.
 */
static struct rendered_glyph* try_fetch_from_cache(u32 char_code,
                                                   b8 fill_on_miss) {
  u8 char_code_hash;
  struct cached_glyph** slot;

  if (char_code < sizeof(g_font_cache.ascii))
    slot = &g_font_cache.ascii[char_code];
  else {
    char_code_hash = get_char_code_hash(char_code);
    slot = &g_font_cache.extra[char_code_hash];
  }

  if (*slot == NULL)
    return fill_on_miss ? cache_glyph(char_code, slot) : NULL;

  if ((*slot)->char_code == char_code) {
    ++(*slot)->hits;
    return &(*slot)->glyph;
  }

  /* If a lot of characters hit that slot, evict the glyph currently occupying
   * it, the most used character should (statistically) retake it.
   */
  if (fill_on_miss /* Don't run this check if we don't plan to fill the slot */
      && ++g_font_cache.wants_slot[char_code_hash] > (*slot)->hits) {
    g_font_cache.wants_slot[char_code_hash] = 0;
    free(*slot);
    *slot = NULL;
  }

  return NULL;
}

static b8 glyph_load_metrics(u32 char_code) {
  FT_Error error;

  error = FT_Load_Char(g_font.face, char_code, FT_LOAD_BITMAP_METRICS_ONLY);
  if (error) {
    log_warn("could not load glyph for character code %#lx: %s",
             char_code, ft_strerror(error));
    return false;
  }

  return true;
}

static struct vec2u32 glyph_advance(u32 char_code) {
  struct rendered_glyph* glyph;
  struct vec2u32 advance = {0};

  glyph = try_fetch_from_cache(char_code, false);
  if (glyph != NULL)
    return glyph->advance;

  if (glyph_load_metrics(char_code)) {
    advance.x = g_font.face->glyph->advance.x;
    advance.y = g_font.face->glyph->advance.y;
  }

  return advance;
}

static struct vec2u32 render_colored_glyph(u32 char_code,
                                           u32 color, u32* buffer,
                                           size_t width, size_t height,
                                           size_t stride_in_px) {
  f32 alpha, inv_alpha;
  u64 x, y, sx, sy;
  u64 buffer_cursor, bitmap_cursor;
  struct color c1, c2;
  struct rendered_glyph glyph, *cached;

  cached = try_fetch_from_cache(char_code, true);
  if (cached != NULL) {
    glyph = *cached;
    goto skip_rendering;
  }

  if (!render_glyph(char_code, &glyph, false))
    /* That glyph does not exist, skip it */
    return (struct vec2u32) { .x = g_font.size_in_pixels << 6, .y = 0 };

skip_rendering:
  sx = glyph.offset.x;
  sy = glyph.offset.y;

  buffer_cursor = sy * stride_in_px + sx;
  bitmap_cursor = 0;
  c1.as_u32 = color;

  for (y = 0; y < glyph.height; ++y) {
    if (sy + y >= height)
      break;

    for (x = 0; x < glyph.width; ++x) {
      if (sx + x >= width)
        break;

      /* Get grayscale color and map it to [0.0f, 1.0f] */
      alpha = (f32)glyph.bitmap[bitmap_cursor + x] / 255.0f;
      inv_alpha = 1.0f - alpha;

      /* Mix the font color and the background color by the alpha channel */
      c2.as_u32 = buffer[buffer_cursor + x];
      c2.r = (c2.r * inv_alpha) + (c1.r * alpha);
      c2.g = (c2.g * inv_alpha) + (c1.g * alpha);
      c2.b = (c2.b * inv_alpha) + (c1.b * alpha);
      buffer[buffer_cursor + x] = c2.as_u32;
    }

    /* Advance cursors */
    bitmap_cursor += glyph.width;
    buffer_cursor += stride_in_px;
  }

  return glyph.advance;
}

static u32 utf8_next_char(const char** s) {
  /* https://it.wikipedia.org/wiki/UTF-8 */
  u8 c;
  size_t consume;
  u32 code;

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

size_t font_string_width(const char* string) {
  u32 char_code;
  size_t w64ths, max_w64ths;
  const char* s = string;

  max_w64ths = w64ths = 0;
  while (*s) {
    char_code = utf8_next_char(&s);
    /* Skip unprintable characters */
    if (!isprint(char_code)) {
      if (char_code == '\n') {
        max_w64ths = max(w64ths, max_w64ths);
        w64ths = 0;
      }
      continue;
    }
    w64ths += glyph_advance(char_code).x;
  }
  max_w64ths = max(w64ths, max_w64ths);

  return (max_w64ths >> 6) + ((max_w64ths & 0x3F) != 0);
}

void font_string_render(const char* string, b8 wrap, u32 color, u32* buffer,
                        size_t buffer_width, size_t buffer_height,
                        size_t buffer_stride_in_pixels) {
  u32 char_code;
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
    /* Skip unprintable characters */
    if (!isprint(char_code)) {
      if (char_code == '\n')
        goto new_line;
      else
        continue;
    }

    advance = render_colored_glyph(char_code, color,
                                   &buffer[cursor + (x64ths >> 6)],
                                   buffer_width - (x64ths >> 6),
                                   buffer_height - (y64ths >> 6),
                                   buffer_stride_in_pixels);

    /* Increment coordinates */
    x64ths += advance.x;
    if (x64ths >= (buffer_width << 6)) {
      /* If we should wrap automatically, find the next LF character */
      if (!wrap) {
        while (*s) {
          char_code = utf8_next_char(&s);
          if (char_code == '\n')
            /* We have a LF character, increment the y coordinate */
            goto new_line;
        }
        /* There's no next LF character, we can return since nothing else
         * will be rendered.
         */
        return;
      }
new_line:
      x64ths = 0;
      y64ths += g_font.size_in_pixels << 6;
      if (y64ths >= (buffer_height << 6))
        return;
      else
        cursor = (y64ths >> 6) * buffer_stride_in_pixels;
    }
  }
}

void font_cache_clear(void) {
  size_t i;
  struct cached_glyph *cached;

  /* Clear ASCII cache */
  for (i = 0; i < ARRAYLENGTH(g_font_cache.all_cached_glyphs); ++i) {
    cached = g_font_cache.all_cached_glyphs[i];
    if (cached != NULL) {
      log_trace("freeing cached glyph for char code %#lx", cached->char_code);
      ASSERT(cached->glyph.owns_bitmap);
      free(cached->glyph.bitmap);
      free(cached);
    }
  }

  memset(&g_font_cache, 0, sizeof(g_font_cache));
}

void font_set_size(size_t pixels) {
  FT_Error error;

  error = FT_Set_Pixel_Sizes(g_font.face, 0, pixels);
  if (error) {
    log_error("could not set character size to %zupx: %s",
              pixels, ft_strerror(error));
    return;
  }

  g_font.size_in_pixels = pixels;
  font_cache_clear();
}

size_t font_get_size(void) {
  return g_font.size_in_pixels;
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
  font_cache_clear();
  FT_Done_Face(g_font.face);
  FT_Done_FreeType(g_library);
}
