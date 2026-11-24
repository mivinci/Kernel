#ifndef _NET_LWIP_CC_H
#define _NET_LWIP_CC_H

/* Net can run without open file descriptors. */
#define LWIP_PLATFORM_ASSERT(message)                                                              \
  do {                                                                                             \
    (void) (message);                                                                              \
    __builtin_trap();                                                                              \
  } while (0)
#endif
