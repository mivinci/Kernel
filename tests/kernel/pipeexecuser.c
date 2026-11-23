#include <fcntl.h>
#include <unistd.h>

/* The old image maps its retained pipe writer onto stdout in Bootfd. */
int
main(int argc, char **argv)
{
  (void) argc;
  (void) argv;
  if (fcntl(0, F_GETFD) != -1)
    return 45;
  return write(1, "e", 1) == 1 && !close(1) ? 43 : 44;
}
