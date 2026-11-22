#ifndef _LIBC_ASSERT_H
#define _LIBC_ASSERT_H
__attribute__((noreturn)) void __libc_assert(const char *expression, const char *file, int line,
                                             const char *function);
#endif

/* Reincluding the header must use the current setting of NDEBUG. */
#undef assert
#ifdef NDEBUG
#define assert(expression) ((void) 0)
#else
#define assert(expression)                                                                         \
  ((expression) ? (void) 0 : __libc_assert(#expression, __FILE__, __LINE__, __func__))
#endif
