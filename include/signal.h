/* Prepaid signal bank, typed delivery and checked user-frame restoration. */
#ifndef _SIGNAL_H
#define _SIGNAL_H
#include <image.h>
#include <space.h>
#include <hal/context.h>
#include <abi/signal.h>
#define SIGNAL_BAD_FRAME_CAUSE UINT64_C(0x8000000000000065)
/* Kernel-only delivery result; never a native wait or user ABI value. */
#define SIG_DELIVER_STOP 0x100
#define SIG_STOPS                                                                                  \
  ((UINT64_C(1) << (SIGNAL_STOP - 1)) | (UINT64_C(1) << (SIGNAL_TSTP - 1)) |                       \
   (UINT64_C(1) << (SIGNAL_TTIN - 1)) | (UINT64_C(1) << (SIGNAL_TTOU - 1)))
typedef struct Sigstat Sigstat;
struct Sigstat
{
  Handle pid;
  u64    image, pending, mask;
  u32    depth, slots, bytes, reserved;
};
/* Each reservation owns one Image cleanup ref, no independent Acct/Set quota. */
int sigreserve(Imageid image);
int sigrelease(Imageid image);
/* Active image only; cookies persist across every reuse of the same bank. */
int sigcookie(Imageid image, u64 *out);
int sigmaskvalid(u64 mask);
int sigactionvalid(u32 signum, const Sigact *action);
int sigactioncheck(Imageid image, u32 signum, const Sigact *action);
int siggetaction(Imageid image, u32 signum, Sigact *out);
int sigsetaction(Imageid image, u32 signum, const Sigact *action);
/* Typed fixture producer; native USER/CHILD/FAULT production and ordinary
   blocked-syscall interruption remain separate from action registration. */
int siginstall(Imageid image, u32 signum, const Sigact *action);
int sigqueue(Imageid image, const Siginfo *info);
int sigvalidinfo(const Siginfo *info);
int sigstopkind(u32 signum);
int sigignored(u32 signum, const Sigact *action);
int sigcancel(Imageid image, u64 mask);
/* A committed child status; pid/ruid come from Life, never user input.
   reason/status follow native WAIT kind/code. Other Siginfo fields are zero. */
int sigchild(Imageid image, Handle child, u32 ruid, u64 reason, u64 status);
/* Completed-user-return boundary only; SIG_DELIVER_STOP|signum requests a
   default stop, other positive results request default exit. */
int sigdeliver(Imageid image, Spaceid space, Hctx *context);
int sigrestore(Imageid image, Spaceid space, Hctx *context, u64 frame);
int sigdiscard(Imageid image);
/* Runnable pending signal, respecting the prepaid handler depth. */
int sigready(Imageid image);
/* Attach the suspended caller mask to the next caught frame. */
int sigwaitreturn(Imageid image, u64 mask);
int sigsetmask(Imageid image, u32 how, u64 mask, u64 *old);
int siggetpending(Imageid image, u64 *out);
int sigbaseline(Imageid target, Imageid source);
/* Trusted fork snapshot into a fresh PREPARING child's already reserved bank.
   Space mappings must already be snapshotted at identical VAs. Copies actions,
   masks and supported active layers, clears pending and rewrites only child
   frame cookies. Parent/image/stack ownership stays separate. No Proc return
   continuation, Rctx/Fix or native fork publication is provided. On failure
   the caller must cancel the unexposed child; cookie sequence never rolls back. */
int sigfork(Imageid source, Spaceid source_space, Imageid target, Spaceid target_space);
int siginitialcheck(Imageid image, u64 mask, u64 defaults);
int siginitial(Imageid image, u64 mask, u64 defaults);
int sigstat(Imageid image, Sigstat *out);
int sigcheck(void);
#endif
