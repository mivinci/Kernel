/* Prepaid signal bank and DEFAULT/IGNORE configuration; delivery/sigreturn pending. */
#ifndef _SIGNAL_H
#define _SIGNAL_H
#include <image.h>
#include <abi/signal.h>
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
int sigsetmask(Imageid image, u32 how, u64 mask, u64 *old);
int siggetpending(Imageid image, u64 *out);
int sigbaseline(Imageid target, Imageid source);
int siginitialcheck(Imageid image, u64 mask, u64 defaults);
int siginitial(Imageid image, u64 mask, u64 defaults);
int sigstat(Imageid image, Sigstat *out);
int sigcheck(void);
#endif
