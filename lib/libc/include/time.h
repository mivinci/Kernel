#ifndef _TIME_H
#define _TIME_H
#include <sys/types.h>
#include <abi/time.h>

typedef int clockid_t;
struct timespec
{
  time_t tv_sec;
  long   tv_nsec;
};

int clock_gettime(clockid_t clock, struct timespec *value);
int clock_getres(clockid_t clock, struct timespec *value);
#endif
