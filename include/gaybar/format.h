#ifndef FORMAT_H_
#define FORMAT_H_

#include <gaybar/types.h>

enum _format_type {
  _FORMAT_TYPE_STRING,
  _FORMAT_TYPE_FLOAT,
  _FORMAT_TYPE_INTEGER
};

struct _format_parameter {
  const char* tag;
  enum _format_type type;
  union {
    const char* value_STRING;
    double      value_FLOAT;
    long        value_INTEGER;
  };
};

/* This function returns true if the buffer was too small, false otherwise */
b8 _format(char* buffer, size_t buffer_size, const char* format,
           struct _format_parameter* params, size_t n_params);

#define FORMAT_PARAM(_tag, _type, _value) \
  { .tag = _tag, .type = _FORMAT_TYPE_##_type, .value_##_type = _value }

#define FORMAT(buffer, buffer_size, format, ...)                \
  _format(buffer, buffer_size, format,                          \
          (struct _format_parameter[]) { __VA_ARGS__ },         \
          sizeof((struct _format_parameter[]) { __VA_ARGS__ })  \
            / sizeof(struct _format_parameter))

#endif
