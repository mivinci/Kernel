#ifndef _LIBC_FORMAT_H
#define _LIBC_FORMAT_H
#include <stdarg.h>
#include <stddef.h>

/* A null text pointer denotes repeated fill bytes. Return an errno value
   on failure; text is borrowed only for the duration of the call. */
typedef int (*Formatemit)(void *context, const char *text, char fill, size_t count);
int __libc_format(char *buffer, size_t size, Formatemit sink, void *context, const char *format,
                  va_list arguments);
#endif
