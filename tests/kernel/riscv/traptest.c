/* Register-sentinel assertions for the explicit trap test image. */
#include <boot.h>
#include <diag.h>
#include "traptest.h"
extern char probe_first[], probe_second[];
extern u64  probe_stack;
static u64  traps;

void
arch_test_trap(Trapframe *frame)
{
  {
    u64 i, expected;
    expected = (u64) (traps == 0 ? probe_first : probe_second);
    if (traps >= 2 || frame->pc != expected ||
        !(frame->cause == 3 && (frame->status & 0x120) == 0x100))
      panic("trap state");
    if (frame->x[2] < (u64) __stack_bottom || frame->x[2] >= (u64) __stack_top ||
        (frame->x[2] & 15))
      panic("saved stack");
    for (i = 1; i < 32; i++) {
      if (i == 2)
        continue;
      if (frame->x[i] != 0x100 + i)
        panic("register sentinel");
    }
    if (frame->x[2] != probe_stack) {
      panic("stack sentinel");
    }
    traps++;
    frame->pc += 4;
  }
}
