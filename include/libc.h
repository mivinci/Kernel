#ifndef _LIBC_H
#define _LIBC_H

#define NULL ((void *)0)
#define EOF  (-1)

typedef unsigned long size_t;
typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

int    vsprintf(char *, const char *, va_list);
size_t strlen(const char *);
void  *memset(void *, int, size_t);
void  *memcpy(void *, const void *, size_t);
void  *memmove(void *, const void *, size_t);
int    memcmp(const void *, const void *, size_t);
int    strcmp(const char *, const char *);
char  *strcpy(char *, const char *);

#endif /* _LIBC_H */
