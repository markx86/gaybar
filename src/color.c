#include <gaybar/color.h>
#include <gaybar/assert.h>

#include <ctype.h>
#include <string.h>

static u8 hex_char_to_u8(char c) {
  ASSERT(isxdigit(c));

  if (c >= '0' && c <= '9')
    return c - '0';
  else if (c >= 'A'&& c <= 'F')
    return (c - 'A') + 10;
  else if (c >= 'a'&& c <= 'f')
    return (c - 'a') + 10;
  else
    ASSERT(false && "unreachable");
}

b8 color_from_hex(const char* s, struct color* c) {
  size_t i, l;

  ASSERT(s != NULL);

  l = strlen(s);

  if (*s != '#')
    goto fail;
  if (l - 1 != 6)
    goto fail;

  for (i = 1; i < l; ++i) {
    if (!isxdigit(s[i]))
      goto fail;
  }

  c->r = (hex_char_to_u8(s[1]) << 4) | hex_char_to_u8(s[2]);
  c->g = (hex_char_to_u8(s[3]) << 4) | hex_char_to_u8(s[4]);
  c->b = (hex_char_to_u8(s[5]) << 4) | hex_char_to_u8(s[6]);
  c->a = 0xFF;

  return true;
fail:
  log_error("invalid color '%s', must be in RGB HEX format", s);
  return false;
}
