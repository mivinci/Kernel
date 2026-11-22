#include <termios.h>

speed_t
cfgetispeed(const struct termios *attributes)
{
  return attributes->c_ispeed;
}

speed_t
cfgetospeed(const struct termios *attributes)
{
  return attributes->c_ospeed;
}
