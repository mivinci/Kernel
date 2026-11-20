#ifndef _STRING_H
#define _STRING_H
#include <stddef.h>
void  *memcpy(void *destination, const void *source, size_t bytes);
void  *memmove(void *destination, const void *source, size_t bytes);
void  *memset(void *destination, int byte, size_t bytes);
int    memcmp(const void *a, const void *b, size_t bytes);
void  *memchr(const void *memory, int byte, size_t bytes);
size_t strlen(const char *string);
size_t strnlen(const char *string, size_t bytes);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t bytes);
char  *strchr(const char *string, int byte);
char  *strrchr(const char *string, int byte);
char  *strstr(const char *string, const char *pattern);
size_t strspn(const char *string, const char *accepted);
size_t strcspn(const char *string, const char *rejected);
char  *strpbrk(const char *string, const char *accepted);
char  *strcpy(char *destination, const char *source);
char  *strncpy(char *destination, const char *source, size_t bytes);
char  *strcat(char *destination, const char *source);
char  *strncat(char *destination, const char *source, size_t bytes);
char  *strerror(int error);
#endif
