#ifndef _TEST_ADOPT_H
#define _TEST_ADOPT_H
#include <sys/channel.h>
#include "private/elf.h"
#include "private/start.h"
#define ADOPT_ENDPOINT     256
#define ADOPT_SELF_CONTROL 257
#define ADOPT_GRANT        (UINT64_C(1) << 63)
#define ADOPT_PARENT_READY 1
#define ADOPT_LEAF_READY   2
#define ADOPT_WAIT_READY   3
isize adoptspawn(const u8 *data, u64 bytes, const char *name, u64 limit, u32 resources,
                 Handle channel, u64 rights, Elfwork *work, Handle *pid, Handle *control);
#endif
