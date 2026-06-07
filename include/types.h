#ifndef _TYPES_H
#define _TYPES_H

/* Kernel-internal type definitions and utility macros */

#define NULL ((void *)0)
#define EOF  (-1)

/*
 * Convenience macros to avoid writing "struct" / "enum" everywhere.
 * Usage:
 *   XDEF_STRUCT(TrapFrame) { int field; };
 *   XDEF_ENUM(Color) { RED, BLUE };
 *   void foo(TrapFrame *tf);  // no "struct" needed
 */
#define XDEF_STRUCT(T)    \
  typedef struct T##_s T; \
  struct T##_s

#define XDEF_ENUM(T) \
  typedef int T;     \
  enum

#define XDEF_HANDLE(T) typedef void *T

#define XDEF_HANDLE_EXPLICIT(T) typedef struct T##_s *T

typedef unsigned long     size_t;
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

#endif /* _TYPES_H */
