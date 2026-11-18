#ifndef _DIAG_H
#define _DIAG_H
#include <types.h>
void          putstr(const char *text);
void          puthex(u64 value);
int           diaghandoff(void);
NORETURN void panic(const char *reason);
#endif
