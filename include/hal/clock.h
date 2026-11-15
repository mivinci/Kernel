#ifndef _HAL_CLOCK_H
#define _HAL_CLOCK_H
#include <types.h>
void hal_clock_init(u64 timebase);
u64  hal_clock_read(void);
u64  hal_clock_freq(void);
#endif
