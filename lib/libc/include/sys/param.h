#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H

#include <limits.h>

#define NBBY CHAR_BIT
#define MAXPATHLEN PATH_MAX
#define setbit(array, bit) ((array)[(bit) / NBBY] |= 1U << ((bit) % NBBY))
#define clrbit(array, bit) ((array)[(bit) / NBBY] &= ~(1U << ((bit) % NBBY)))
#define isset(array, bit) (((array)[(bit) / NBBY] & (1U << ((bit) % NBBY))) != 0)
#define isclr(array, bit) (!isset(array, bit))

#endif
