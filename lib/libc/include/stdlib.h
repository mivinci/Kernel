#ifndef _STDLIB_H
#define _STDLIB_H
#include <types.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void         *malloc(size_t bytes);
void         *calloc(size_t count, size_t bytes);
void         *realloc(void *pointer, size_t bytes);
void          free(void *pointer);
int           atexit(void (*function)(void));
NORETURN void exit(int status);
char         *getenv(const char *name);
int           setenv(const char *name, const char *value, int overwrite);
int           unsetenv(const char *name);
long          strtol(const char *string, char **end, int base);
unsigned long strtoul(const char *string, char **end, int base);
#endif
