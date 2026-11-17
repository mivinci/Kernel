/* Prepaid signal bank, typed delivery and checked user-frame restoration. */
#ifndef _SIGNAL_H
#define _SIGNAL_H
#include <image.h>
#include <space.h>
#include <hal/context.h>
#include <abi/signal.h>
#define SIGNAL_BAD_FRAME_CAUSE UINT64_C(0x8000000000000065)
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
int siggetaction(Imageid image, u32 signum, Sigact *out);
int sigsetaction(Imageid image, u32 signum, const Sigact *action);
/* Typed fixture producer; native USER/CHILD/FAULT production and ordinary
   blocked-syscall interruption remain separate from action registration. */
int siginstall(Imageid image, u32 signum, const Sigact *action);
int sigqueue(Imageid image, const Siginfo *info);
/* Completed-user-return boundary only; positive result requests default exit. */
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
int siginitialcheck(Imageid image, u64 mask, u64 defaults);
int siginitial(Imageid image, u64 mask, u64 defaults);
int sigstat(Imageid image, Sigstat *out);
int sigcheck(void);
#endif
