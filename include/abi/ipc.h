#ifndef _ABI_IPC_H
#define _ABI_IPC_H
#ifndef __ASSEMBLER__
#include <types.h>
typedef struct Msg Msg;
struct Msg
{
  u64 words[4];
};
STATIC_ASSERT(sizeof(Msg) == 32, msg_size);
#endif
#define RQ_QUEUED           1
#define RQ_ACCEPTED         2
#define RQ_TERMINAL         3
#define RQ_CONSUMED         1
#define RQ_FINISHED         2
#define RQ_RELEASED         4
#define RQ_CANCEL_REQUESTED 8
#define IPC_NONE            0
#define IPC_CALL            1
#define IPC_SEND            2
#define IPC_EVENT           3
#define IPC_RESOURCE_REPLY  4
#define REPLY_RESOURCE      1
#define CALL_CONTROL        8
#define CALL_ASYNC          1
#define CALL_RECV_MEM       2
#define CALL_RECV_CALL      4
#define RECV_NONBLOCK       1
#define RECV_EVENTS_ONLY    2
#define RQ_FINISH           1
#define ACK_RESULT          1
#define ACK_RELEASE         2
#endif
