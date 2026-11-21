#ifndef _TEST_VIRTIOUSER_H
#define _TEST_VIRTIOUSER_H
#include <types.h>
#define VTEST_DMA_BASE  0x2100000UL
#define VTEST_PAGES     9
#define VTEST_IRQ_TAG   258
#define VTEST_MEMBERS   8
#define VTEST_RESIZE    32
#define VTEST_CAPACITY  33
#define VTEST_PARALLEL  34
#define VTEST_DMA_TRACE 40
#define VTEST_TRACE     (VTEST_DMA_TRACE + VTEST_MEMBERS * VTEST_PAGES)
int  virtioread(void *context, u32 offset, u32 *out);
int  virtioreadbyte(void *context, u32 offset, u8 *out);
int  virtiowrite(void *context, u32 offset, u32 value);
void virtiofence(void *context, u32 phase);
#endif
