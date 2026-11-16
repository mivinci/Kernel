#ifndef _STRING_H
#define _STRING_H
#include <stddef.h>
void  *memcpy(void *destination, const void *source, size_t bytes);
void  *memmove(void *destination, const void *source, size_t bytes);
void  *memset(void *destination, int byte, size_t bytes);
int    memcmp(const void *a, const void *b, size_t bytes);
size_t strlen(const char *string);
int    strcmp(const char *a, const char *b);
char  *strchr(const char *string, int byte);
char  *strerror(int error);
#endif
