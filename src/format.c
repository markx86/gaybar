#include <gaybar/format.h>
#include <gaybar/assert.h>
#include <gaybar/util.h>

#include <stdio.h>
#include <string.h>

struct fmtstr {
  const char* ptr;
};

struct fmtbuf {
  char* ptr;
  char* end;
  b8 full;
};

struct fmtflags {
  u32 is_escaped_tag : 1;
  u32 unused         : 31;
};

static inline char fmtstr_peek(struct fmtstr* fmt) {
  return *fmt->ptr;
}

static inline char fmtstr_next(struct fmtstr* fmt) {
  if (*fmt->ptr)
    return *(fmt->ptr++);
  else
    return '\0';
}

static inline void fmtstr_init(struct fmtstr* fmt, const char* format) {
  fmt->ptr = format;
}

static inline void fmtbuf_putc(struct fmtbuf* buf, char c) {
  if (!buf->full) {
    *(buf->ptr++) = c;
    buf->full = buf->ptr >= buf->end;
  }
}

static inline void fmtbuf_puts(struct fmtbuf* buf, const char* s) {
  while (*s && !buf->full)
    fmtbuf_putc(buf, *(s++));
}

static inline int fmtbuf_is_full(struct fmtbuf* buf) {
  return buf->full;
}

static inline void fmtbuf_init(struct fmtbuf* buf,
                               char* buffer, size_t buffer_size) {
  buf->ptr = buffer;
  buf->end = buffer + buffer_size;
  buf->full = false;
}

static b8 get_tag(struct fmtstr* fmt, char* tag_buffer, size_t buffer_size) {
  char c;
  b8 success = false;
  while ((c = fmtstr_next(fmt))) {
    if (c == '}') {
      success = true;
      break;
    }
    *(tag_buffer++) = c;
    if (--buffer_size == 1)
      break;
  }
  *tag_buffer = '\0';
  return success;
}

static struct _format_parameter*
find_param_by_tag(const char* tag,
                  struct _format_parameter* params, size_t n_params) {
  struct _format_parameter* param;
  for (param = params; n_params > 0; --n_params) {
    if (strcmp(param->tag, tag) == 0)
      return param;
    ++param;
  }
  return NULL;
}

static b8 format_param(struct fmtbuf* buf, struct _format_parameter* param) {
  size_t buf_size, written;

  buf_size = buf->end - buf->ptr;

  switch (param->type) {
    case _FORMAT_TYPE_STRING:
      written = snprintf(buf->ptr, buf_size, "%s", param->value_STRING);
      break;
    case _FORMAT_TYPE_FLOAT:
      written = snprintf(buf->ptr, buf_size, "%.2f", param->value_FLOAT);
      break;
    case _FORMAT_TYPE_INTEGER:
      written = snprintf(buf->ptr, buf_size, "%ld", param->value_INTEGER);
      break;
    default:
      return false;
  }

  /* Mark the buffer as full if there was no space for snprintf */
  buf->full = written >= buf_size;
  buf->ptr += min(written, buf_size);
  return true;
}

int _format(char* buffer, size_t buffer_size, const char* format,
            struct _format_parameter* params, size_t n_params) {
  char c, tag[64];
  struct _format_parameter* param;
  struct fmtbuf buf;
  struct fmtstr fmt;
  struct fmtflags flags = {0};

  ASSERT(buffer != NULL);
  ASSERT(format != NULL);
  ASSERT(params != NULL);

  fmtbuf_init(&buf, buffer, buffer_size);
  fmtstr_init(&fmt, format);

  while (!fmtbuf_is_full(&buf) && (c = fmtstr_next(&fmt))) {
    if (c != '{' || flags.is_escaped_tag) {
      fmtbuf_putc(&buf, c);
      /* Skip second closing parenthesis of escape sequence */
      if (flags.is_escaped_tag &&
          c == '}' && fmtstr_peek(&fmt) == '}') {
        flags.is_escaped_tag = false;
        fmtstr_next(&fmt);
      }
      continue;
    }

    if (fmtstr_peek(&fmt) == '{') {
      fmtbuf_putc(&buf, fmtstr_next(&fmt));
      flags.is_escaped_tag = true;
      continue;
    }

    if (!get_tag(&fmt, tag, sizeof(tag))) {
      fmtbuf_puts(&buf, tag);
      continue;
    }

    param = find_param_by_tag(tag, params, n_params);
    if (param == NULL) {
fail_format:
      fmtbuf_putc(&buf, c);
      fmtbuf_puts(&buf, tag);
      fmtbuf_putc(&buf, '}');
      continue;
    }

    if (!format_param(&buf, param))
      goto fail_format;
  }

  return fmtbuf_is_full(&buf);
}
