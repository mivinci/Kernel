#ifndef _SETJMP_H
#define _SETJMP_H
#include <stdint.h>

/* Opaque storage for the current SDK's two target calling conventions. */
typedef uint64_t jmp_buf[22];

int                            setjmp(jmp_buf environment) __attribute__((returns_twice));
__attribute__((noreturn)) void longjmp(jmp_buf environment, int value);
#endif
