#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "private/syscall.h"
#include "private/start.h"
volatile u64 frame_trace[32];
static u8    saved[1024];
extern u64   frame_fp(u64 mode);
#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      frame_trace[31] = (n);                                                                       \
      _exit(n);                                                                                    \
    }                                                                                              \
  } while (0)
static void
handler(u64 signum, const Siginfo *info, void *context)
{
  UNUSED(context);
  REQUIRE(info->signum == signum && !info->reserved0 && !info->reserved[0] && !info->reserved[1] &&
              !info->code && !info->pid && !info->uid && info->status == 0x12345678,
          151);
  frame_trace[12]++;
  if (frame_trace[14])
    frame_trace[13]++;
  REQUIRE(frame_fp(frame_trace[14] ? 3 : 2) == (frame_trace[14] ? 2 : 1), 152);
}
static NORETURN void
trampoline(u64 signum, const Siginfo *info, void *context, u64 function, u64 address, u64 flags)
{
  Sighead *head = (Sighead *) address;
  u64      mode = frame_trace[1], mask, expected, old;
  Sigact   action;
  REQUIRE(head->magic == SIGNAL_FRAME_MAGIC && (head->bytes == 688 || head->bytes == 944) &&
              !head->flags && !head->reserved && head->cookie && head->depth >= 1 &&
              head->depth <= 2,
          153);
  REQUIRE((u64) info == address + 64 && (u64) context == address + 128 &&
              (u64) function == frame_trace[2],
          154);
  frame_trace[14] = head->depth == 2;
  if (head->depth == 1) {
    frame_trace[4] = address;
    frame_trace[6] = head->cookie;
    REQUIRE(!head->previous, 155);
  } else {
    frame_trace[5] = address;
    frame_trace[7] = head->cookie;
    REQUIRE(head->previous == frame_trace[4] && head->cookie != frame_trace[6], 156);
  }
  expected = 1 | ((u64) 1 << (SIGNAL_TERM - 1));
  if (mode != 9)
    expected |= (u64) 1 << (SIGNAL_USR1 - 1);
  if (head->depth == 2 && mode != 9)
    expected |= ((u64) 1 << (SIGNAL_USR2 - 1)) | ((u64) 1 << (SIGNAL_PIPE - 1));
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, 0, &mask) && mask == expected, 157);
  if (mode == 4) {
    if (!frame_trace[15]) {
      memcpy(saved, (const void *) address, head->bytes);
      frame_trace[15] = 1;
    } else {
      REQUIRE(head->cookie != ((Sighead *) saved)->cookie, 158);
      frame_trace[10] = head->cookie;
      memcpy((void *) address, saved, head->bytes);
      __sys_sigreturn((void *) address);
    }
  }
  if (mode == 3 && head->depth == 2)
    __sys_sigreturn((void *) frame_trace[4]);
  if (mode == 8) {
    REQUIRE((flags & SIGNAL_RESETHAND) && !__sys_sigaction(SIGNAL_USR1, NULL, &action) &&
                action.kind == SIGNAL_DEFAULT,
            159);
  }
  ((void (*)(u64, const Siginfo *, void *))(uintptr_t) function)(signum, info, context);
  if (head->depth == 1 && (mode == 1 || mode == 3 || mode == 9)) {
    frame_trace[0] = 2;
    while (!frame_trace[13])
      REQUIRE(!__sys_yield(), 160);
    frame_trace[14] = 0;
    REQUIRE(frame_fp(2) == 2, 161);
    REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, 0, &mask) && mask == expected, 162);
  }
  if (head->depth == 2) {
    REQUIRE(!__sys_sigmask(SIGNAL_SETMASK, SIGNAL_SUPPORTED, &old), 163);
  }
  if (mode == 0)
    head->restore_mask = ((u64) 1 << (SIGNAL_USR2 - 1)) | SIGNAL_UNMASKABLE;
  if (mode == 2)
    ((u64 *) context)[33] ^= 1;
  if (mode == 5)
    ((u64 *) context)[32] = 0x800000;
  if (mode == 7)
    _exit(42);
  __sys_sigreturn((void *) address);
}
int
main(int argc, char **argv)
{
  u64 mode, old, mask;
  REQUIRE(argc == 1 && argv[0][0] >= '0' && argv[0][0] <= '9', 164);
  mode = (u64) (argv[0][0] - '0');
  frame_trace[1] = mode;
  frame_trace[2] = (u64) (uintptr_t) handler;
  frame_trace[3] = (u64) (uintptr_t) trampoline;
  REQUIRE(frame_fp(1) == 0, 165);
  REQUIRE(!__sys_sigmask(SIGNAL_SETMASK, 1, &old) && !old, 166);
  if (mode == 6)
    __sys_sigreturn((void *) 0x3ffffef000UL);
  frame_trace[0] = 1;
  while (!frame_trace[12])
    REQUIRE(!__sys_yield(), 167);
  if (mode == 4 || mode == 8) {
    REQUIRE(frame_fp(1) == 1, 168);
    frame_trace[0] = 3;
    for (;;)
      REQUIRE(!__sys_yield(), 169);
  }
  REQUIRE(mode == 0 || mode == 1 || mode == 9, 170);
  REQUIRE(frame_fp(1) == 1, 171);
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, 0, &mask) &&
              mask == (mode == 0 ? ((u64) 1 << (SIGNAL_USR2 - 1)) : 1),
          172);
  REQUIRE(frame_trace[12] == (mode == 0 ? 1U : 2U), 173);
  frame_trace[0] = 99;
  return 42;
}
