#ifndef _ABI_RESOURCE_H
#define _ABI_RESOURCE_H
#include <stdint.h>
/* Existing common entry rights, shared with userspace grant/query adapters. */
#define RESOURCE_USE   1
#define RESOURCE_GRANT (UINT64_C(1) << 63)
#define RESOURCE_INHERIT_FORK 1
#define RESOURCE_INHERIT_EXEC 2
/* Frozen query RESOURCE subtypes; private Local transaction states are not ABI. */
#define RESOURCE_CREATE     1
#define RESOURCE_SPACE      2
#define RESOURCE_ALLOC      3
#define RESOURCE_MEM        4
#define RESOURCE_CALL       5
#define RESOURCE_SEND       6
#define RESOURCE_NOTIFY     7
#define RESOURCE_CONTROL    8
#define RESOURCE_OBSERVE    9
#define RESOURCE_IRQ_SOURCE 10
#define RESOURCE_IRQ_BIND   11
#define RESOURCE_TERMINAL   12
#define RESOURCE_EXEC_VIEW  13
#define RESOURCE_CONSTRUCT  14
#define RESOURCE_SYSTEM     15
#endif
