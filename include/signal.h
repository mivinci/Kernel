/* Prepaid per-thread signal storage; not native signal delivery or sigreturn. */
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
int sigstat(Imageid image, Sigstat *out);
int sigcheck(void);
#endif
