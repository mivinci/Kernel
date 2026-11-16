#ifndef _STDLIB_H
#define _STDLIB_H
#include <types.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
int           atexit(void (*function)(void));
NORETURN void exit(int status);
char         *getenv(const char *name);
#endif
