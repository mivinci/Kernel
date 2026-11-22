#ifndef _STDIO_H
#define _STDIO_H
#include <stddef.h>
#include <stdarg.h>
/* Memory formatting: integers, pointer, byte string/character and %%. */
int snprintf(char *buffer, size_t size, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments)
    __attribute__((format(printf, 3, 0)));
int dprintf(int fd, const char *format, ...) __attribute__((format(printf, 2, 3)));
int vdprintf(int fd, const char *format, va_list arguments) __attribute__((format(printf, 2, 0)));
#endif
