#ifndef _TIME_H
#define _TIME_H
#include <sys/types.h>
#include <abi/time.h>

typedef int     clockid_t;
typedef int64_t clock_t;
struct timespec
{
  time_t tv_sec;
  long   tv_nsec;
};

int    clock_gettime(clockid_t clock, struct timespec *value);
int    clock_getres(clockid_t clock, struct timespec *value);
int    nanosleep(const struct timespec *request, struct timespec *remaining);
time_t time(time_t *result);
#endif
