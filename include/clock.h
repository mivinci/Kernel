#ifndef _CLOCK_H
#define _CLOCK_H
#include <types.h>
int clockinit(void);
int clocktime(u64 id, isize *seconds, u64 *nanos);
int clockres(u64 id, isize *seconds, u64 *nanos);
int clockmono(u64 *nanos);
#endif
