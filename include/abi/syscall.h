#ifndef _ABI_SYSCALL_H
#define _ABI_SYSCALL_H
#define SYS_EXIT          0x01
#define SYS_YIELD         0x02
#define SYS_NOTIFY        0x20
#define SYS_ALARMSET      0x24
#define SYS_ALARMCANCEL   0x25
#define SYS_CLOCK_GETTIME 0x21
#define SYS_CLOCK_GETRES  0x22
#define SYS_CALL          0x10
#define SYS_SEND          0x11
#define SYS_RECV          0x12
#define SYS_REPLY         0x13
#define SYS_REPLY_RECV    0x14
#define SYS_DEFER         0x15
#define SYS_RQPOLL        0x16
#define SYS_RQWAIT        0x17
#define SYS_RQCANCEL      0x18
#define SYS_RQACK         0x19
#define SYS_RQFINISH      0x1a
#define SYS_RQCHECK       0x1c
#define SYS_COPY          0x03
#define SYS_ALLOC         0x30
#define SYS_FREE          0x31
#define SYS_MAP           0x32
#define SYS_UNMAP         0x33
#define SYS_SEAL          0x34
#define SYS_CONFIGURE     0x41
#define SYS_START         0x42
#define SYS_DESTROY       0x43
#define SYS_GRANT         0x46
#define SYS_REVOKE        0x47
#define SYS_INHERIT       0x56
#define SYS_CONNECT       0x55
#define SYS_GETID         0x4f
#endif
