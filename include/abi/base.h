#ifndef _ABI_BASE_H
#define _ABI_BASE_H
#include <stdint.h>
typedef int64_t Handle;
#define HANDLE_PROCESS        1
#define HANDLE_RESOURCE       2
#define HANDLE_REQUEST        3
#define HANDLE_RPC            4
#define HANDLE_SESSION        5
#define HANDLE_GROUP          6
#define HANDLE_TERMINAL       7
#define HANDLE_GENERATION_MAX ((UINT64_C(1) << 43) - 1)
#define ERR_INVALID           1
#define ERR_FAULT             2
#define ERR_BAD_HANDLE        3
#define ERR_STALE             4
#define ERR_DENIED            5
#define ERR_STATE             6
#define ERR_BUSY              7
#define ERR_LIMIT             8
#define ERR_CAPACITY          9
#define ERR_NOMEM             10
#define ERR_WOULD_BLOCK       11
#define ERR_INTERRUPTED       12
#define ERR_CANCELLED         13
#define ERR_PEER              14
#define ERR_REVOKED           15
#define ERR_DEADLOCK          16
#define ERR_DEPTH             17
#define ERR_NOTSUP            18
#define ERR_NOSYS             19
#define ERR_OVERFLOW          20
#define ERR_TIMEDOUT          21
#endif
