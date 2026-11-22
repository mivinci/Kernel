#ifndef _PROTO_TTY_H
#define _PROTO_TTY_H
#include <types.h>

#define TTY_GETATTR 1

/* Private init-to-tty Boot tags. argv[1] is the manifest service number. */
#define TTY_INIT_CALL     0x110
#define TTY_RX_RING       0x111
#define TTY_TX_RING       0x112
#define TTY_SERIAL_NOTIFY 0x113

#define TTY_ICRNL  1
#define TTY_OPOST  1
#define TTY_ONLCR  2
#define TTY_ICANON 1
#define TTY_ECHO   2
#define TTY_ISIG   4
#define TTY_CS8    1

#define TTY_VINTR  0
#define TTY_VQUIT  1
#define TTY_VERASE 2
#define TTY_VKILL  3
#define TTY_VEOF   4
#define TTY_VTIME  5
#define TTY_VMIN   6
#define TTY_VSUSP  7

typedef struct Termarg Termarg;
struct Termarg
{
  u64 iflag, oflag, cflag, lflag, ispeed, ospeed;
  u8  cc[32];
};
STATIC_ASSERT(sizeof(Termarg) == 80, tty_termarg_size);
STATIC_ASSERT(offsetof(Termarg, iflag) == 0, tty_iflag_offset);
STATIC_ASSERT(offsetof(Termarg, oflag) == 8, tty_oflag_offset);
STATIC_ASSERT(offsetof(Termarg, cflag) == 16, tty_cflag_offset);
STATIC_ASSERT(offsetof(Termarg, lflag) == 24, tty_lflag_offset);
STATIC_ASSERT(offsetof(Termarg, ispeed) == 32, tty_ispeed_offset);
STATIC_ASSERT(offsetof(Termarg, ospeed) == 40, tty_ospeed_offset);
STATIC_ASSERT(offsetof(Termarg, cc) == 48, tty_cc_offset);
#endif
