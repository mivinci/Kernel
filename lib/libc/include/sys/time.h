#ifndef _SYS_TIME_H
#define _SYS_TIME_H
#include <sys/types.h>
typedef long suseconds_t;
struct timeval
{
  time_t      tv_sec;
  suseconds_t tv_usec;
};

int gettimeofday(struct timeval *value, void *timezone);
#endif
