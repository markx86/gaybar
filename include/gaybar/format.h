#ifndef FORMAT_H_
#define FORMAT_H_

#include <gaybar/types.h>

enum _format_type {
  _FORMAT_TYPE_STRING  = 0,
  _FORMAT_TYPE_FLOAT   = 1,
  _FORMAT_TYPE_INTEGER = 2
};

typedef size_t (*format_custom_formatter_t)(char*, size_t, void*);

struct _format_parameter {
  const char* tag;
  union {
    enum _format_type type;
    format_custom_formatter_t fmter;
  };
  union {
    const char* value_STRING;
    double      value_FLOAT;
    long        value_INTEGER;
    void*       value_CUSTOM;
  };
};

/* This function returns true if the buffer was too small, false otherwise */
b8 _format(char* buffer, size_t buffer_size, const char* format,
           struct _format_parameter* params, size_t n_params);

#define FORMAT_PARAM(_tag, _type, _value) \
  { .tag = _tag, .type = _FORMAT_TYPE_##_type, .value_##_type = _value }

#define FORMAT_PARAM_CUSTOM(_tag, _fmter, _value) \
  { .tag = _tag, .fmter = _fmter, .value_CUSTOM = (void*)(_value) }

#define FORMAT(buffer, buffer_size, format, ...)                \
  _format(buffer, buffer_size, format,                          \
          (struct _format_parameter[]) { __VA_ARGS__ },         \
          sizeof((struct _format_parameter[]) { __VA_ARGS__ })  \
            / sizeof(struct _format_parameter))

#endif
