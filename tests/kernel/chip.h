#ifndef _TEST_CHIP_H
#define _TEST_CHIP_H
#include <dtb.h>
void chipcpu(const Dtinfo *info, u64 cpu);
void chipbegin(void);
void chipend(void);
u32  chipmaskcheck(u32 number);
u32  chipspecial(void);
u32  chipmasked(void);
#endif
