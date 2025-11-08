#ifndef _LIBC_H
#define _LIBC_H

#define NULL ((void *)0)
#define EOF  (-1)

typedef unsigned long size_t;
typedef char         *va_list;

#define va_start(ap, fmt) (ap = (va_list) & fmt + sizeof(fmt))
#define va_arg(ap, type)  (*(type *)((ap += sizeof(type)) - sizeof(type)))
#define va_end(ap)        (ap = NULL)

int    vsprintf(char *, const char *, va_list);
size_t strlen(const char *);

#endif /* _LIBC_H */
