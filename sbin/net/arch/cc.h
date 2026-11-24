#ifndef _NET_LWIP_CC_H
#define _NET_LWIP_CC_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef SSIZE_MAX
#define SSIZE_MAX INT64_MAX
#endif

/* lwIP uses atoi to decode the decimal interface number. */
#define atoi(text) ((int) strtol((text), NULL, 10))

#define LWIP_PLATFORM_ASSERT(message)                                                              \
  do {                                                                                             \
    fputs((message), stderr);                                                                      \
    abort();                                                                                       \
  } while (0)
#endif
