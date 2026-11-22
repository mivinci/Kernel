#ifndef _STDIO_H
#define _STDIO_H
#include <stddef.h>
#include <stdarg.h>
#define EOF (-1)
typedef struct Stdiofile FILE;
extern FILE *const       stdout;
extern FILE *const       stderr;
int                      fflush(FILE *stream);
int                      ferror(FILE *stream);
void                     clearerr(FILE *stream);
size_t                   fwrite(const void *data, size_t size, size_t count, FILE *stream);
int                      fputs(const char *text, FILE *stream);
int                      fputc(int byte, FILE *stream);
int                      putc(int byte, FILE *stream);
int fprintf(FILE *stream, const char *format, ...) __attribute__((format(printf, 2, 3)));
int vfprintf(FILE *stream, const char *format, va_list arguments)
    __attribute__((format(printf, 2, 0)));
int printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
int vprintf(const char *format, va_list arguments) __attribute__((format(printf, 1, 0)));
/* Memory formatting: integers, pointer, byte string/character and %%. */
int snprintf(char *buffer, size_t size, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments)
    __attribute__((format(printf, 3, 0)));
int dprintf(int fd, const char *format, ...) __attribute__((format(printf, 2, 3)));
int vdprintf(int fd, const char *format, va_list arguments) __attribute__((format(printf, 2, 0)));
#endif
