#ifndef _ABI_PROC_H
#define _ABI_PROC_H
#define CREATE_USE           1
#define CREATE_CREDENTIAL    2
#define CREATE_SERVICE       4
#define CREATE_RIGHTS        7
#define CONTROL_QUERY        0x01
#define CONTROL_TERMINATE    0x02
#define CONTROL_BUDGET       0x04
#define CONTROL_IRQ          0x08
#define CONTROL_REVOKE       0x10
#define CONTROL_CONNECT      0x20
#define CONTROL_INSTALL      0x40
#define CONTROL_RIGHTS       0x7f
#define CONNECT_CALL         5
#define CONNECT_SEND         6
#define CONNECT_NOTIFY       7
#define CONSTRUCT_MAP        1
#define CONSTRUCT_CONFIG     2
#define CONSTRUCT_PUBLISH    4
#define CONSTRUCT_ABORT      8
#define CONSTRUCT_ALLOC      16
#define PROCESS_CONSTRUCT    1
#define PROCESS_READY        2
#define PROCESS_RUNNING      3
#define PROCESS_BLOCKED      4
#define PROCESS_EXITING      5
#define PROCESS_STOPPED      6
#define WAIT_WNOHANG         1
#define WAIT_WUNTRACED       2
#define WAIT_WCONTINUED      4
#define WAIT_EXITED          1
#define WAIT_STOPPED         2
#define WAIT_CONTINUED       3
#define WAIT_SIGNALLED       0x100
#define WAIT_CORE            0x200
#define WAIT_FAULT           0x400
#define WAIT_SELECT_ANY      0
#define WAIT_SELECT_PID      1
#define WAIT_SELECT_GROUP    2
#define PROCESS_WAIT_NONE    0
#define PROCESS_WAIT_RECV    1
#define PROCESS_WAIT_RESULT  2
#define PROCESS_WAIT_FINISH  3
#define PROCESS_WAIT_DESTROY 4
#define PROCESS_WAIT_CHILD   5
#define QUERY_PROC           1
#define QUERY_RQ             2
#define QUERY_ACCT           3
#define QUERY_LIFE           4
#define QUERY_RESOURCE       5
#define QUERY_BOOT_RESOURCE  6
#define QUERY_RECORDS        16
#ifndef __ASSEMBLER__
#include <types.h>
#include <abi/base.h>
typedef struct Createarg Createarg;
typedef struct Config    Config;
typedef struct Budgetarg Budgetarg;
typedef struct Queryrec  Queryrec;
struct Createarg
{
  u64 memlimit, exec_limit;
  u32 resources, pending, ruid, euid, rgid, egid;
  u64 reserved;
};
STATIC_ASSERT(sizeof(Createarg) == 48, createarg_size);
STATIC_ASSERT(offsetof(Createarg, memlimit) == 0, createarg_limit);
STATIC_ASSERT(offsetof(Createarg, exec_limit) == 8, createarg_peak);
STATIC_ASSERT(offsetof(Createarg, resources) == 16, createarg_resources);
STATIC_ASSERT(offsetof(Createarg, pending) == 20, createarg_pending);
STATIC_ASSERT(offsetof(Createarg, ruid) == 24, createarg_ruid);
STATIC_ASSERT(offsetof(Createarg, euid) == 28, createarg_euid);
STATIC_ASSERT(offsetof(Createarg, rgid) == 32, createarg_rgid);
STATIC_ASSERT(offsetof(Createarg, egid) == 36, createarg_egid);
STATIC_ASSERT(offsetof(Createarg, reserved) == 40, createarg_reserved);
struct Config
{
  u64 entry, stack, boot, bootsz, sigmask, sigdefault;
};
struct Budgetarg
{
  u64 memlimit, exec_limit, max_instances, reserved;
};
struct Queryrec
{
  u64    type;
  Handle pid;
  u64    image, stamp, data[20];
};
STATIC_ASSERT(sizeof(Queryrec) == 192, queryrec_size);
STATIC_ASSERT(offsetof(Queryrec, type) == 0, queryrec_type);
STATIC_ASSERT(offsetof(Queryrec, pid) == 8, queryrec_pid);
STATIC_ASSERT(offsetof(Queryrec, image) == 16, queryrec_image);
STATIC_ASSERT(offsetof(Queryrec, stamp) == 24, queryrec_stamp);
STATIC_ASSERT(offsetof(Queryrec, data) == 32, queryrec_data);
STATIC_ASSERT(sizeof(Budgetarg) == 32, budgetarg_size);
STATIC_ASSERT(offsetof(Budgetarg, memlimit) == 0, budgetarg_limit);
STATIC_ASSERT(offsetof(Budgetarg, exec_limit) == 8, budgetarg_peak);
STATIC_ASSERT(offsetof(Budgetarg, max_instances) == 16, budgetarg_instances);
STATIC_ASSERT(offsetof(Budgetarg, reserved) == 24, budgetarg_reserved);
STATIC_ASSERT(sizeof(Config) == 48, config_size);
STATIC_ASSERT(offsetof(Config, entry) == 0, config_entry);
STATIC_ASSERT(offsetof(Config, stack) == 8, config_stack);
STATIC_ASSERT(offsetof(Config, boot) == 16, config_boot);
STATIC_ASSERT(offsetof(Config, bootsz) == 24, config_bootsz);
STATIC_ASSERT(offsetof(Config, sigmask) == 32, config_sigmask);
STATIC_ASSERT(offsetof(Config, sigdefault) == 40, config_sigdefault);
#endif
#endif
