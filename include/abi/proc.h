#ifndef _ABI_PROC_H
#define _ABI_PROC_H
#include <types.h>
#define CONSTRUCT_MAP     1
#define CONSTRUCT_CONFIG  2
#define CONSTRUCT_PUBLISH 4
#define CONSTRUCT_ABORT   8
#define CONSTRUCT_ALLOC   16
typedef struct Config Config;
struct Config
{
  u64 entry, stack, boot, bootsz, sigmask, sigdefault;
};
STATIC_ASSERT(sizeof(Config) == 48, config_size);
STATIC_ASSERT(offsetof(Config, entry) == 0, config_entry);
STATIC_ASSERT(offsetof(Config, stack) == 8, config_stack);
STATIC_ASSERT(offsetof(Config, boot) == 16, config_boot);
STATIC_ASSERT(offsetof(Config, bootsz) == 24, config_bootsz);
STATIC_ASSERT(offsetof(Config, sigmask) == 32, config_sigmask);
STATIC_ASSERT(offsetof(Config, sigdefault) == 40, config_sigdefault);
#endif
