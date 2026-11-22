#ifndef _TERMIOS_H
#define _TERMIOS_H
#include <stdint.h>

typedef uint8_t  cc_t;
typedef uint64_t speed_t;
typedef uint64_t tcflag_t;

#define NCCS 32

#define ICRNL  1
#define OPOST  1
#define ONLCR  2
#define CS8    1
#define ICANON 1
#define ECHO   2
#define ISIG   4

#define VINTR  0
#define VQUIT  1
#define VERASE 2
#define VKILL  3
#define VEOF   4
#define VTIME  5
#define VMIN   6
#define VSUSP  7

struct termios
{
  tcflag_t c_iflag, c_oflag, c_cflag, c_lflag;
  speed_t  c_ispeed, c_ospeed;
  cc_t     c_cc[NCCS];
};

speed_t cfgetispeed(const struct termios *attributes);
speed_t cfgetospeed(const struct termios *attributes);
int     tcgetattr(int fd, struct termios *attributes);
#endif
