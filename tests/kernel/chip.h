#ifndef _TEST_CHIP_H
#define _TEST_CHIP_H
#include <types.h>
void chipbegin(void);
void chipend(void);
u32  chipmaskcheck(u32 number);
u32  chipspecial(void);
u32  chipmasked(void);
#endif
