#include <hal/boot.h>
void
hal_debug_sync(void)
{
  __sync_synchronize();
}
