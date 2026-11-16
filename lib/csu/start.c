#include "private/libc.h"
extern int main(int argc, char **argv);
NORETURN void
csustart(const Boot *boot)
{
  libcstart(boot, main);
}
