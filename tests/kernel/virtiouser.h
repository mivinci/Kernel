#ifndef _TEST_VIRTIOUSER_H
#define _TEST_VIRTIOUSER_H
#include <types.h>
int virtioread(void *context, u32 offset, u32 *out);
int virtiowrite(void *context, u32 offset, u32 value);
#endif
