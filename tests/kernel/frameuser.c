#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "private/syscall.h"
#include "private/start.h"
#include "private/context.h"
#include "private/fix.h"
#include "private/elf.h"
#include <abi/syscall.h>
volatile u64    frame_trace[32];
static u8       saved[1024];
static Rbank    runtime_contexts;
static Rkey     runtime_key;
static Connpeer context_self;
extern u64      frame_fp(u64 mode);
extern u64      frame_wait(void);
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
  sigset_t set, current, pending;
  UNUSED(context);
  REQUIRE(info->signum == signum && !info->reserved0 && !info->reserved[0] && !info->reserved[1] &&
              !info->code && !info->pid && !info->uid && info->status == 0x12345678,
          151);
  frame_trace[12]++;
  if (frame_trace[14])
    frame_trace[13]++;
  REQUIRE(frame_fp(frame_trace[14] ? 3 : 2) == (frame_trace[14] ? 2 : 1), 152);
  REQUIRE(!sigemptyset(&set) && !sigaddset(&set, SIGUSR1) && sigismember(&set, SIGUSR1) == 1 &&
              !sigdelset(&set, SIGUSR1) && !sigismember(&set, SIGUSR1),
          181);
  REQUIRE(!sigprocmask(999, NULL, &current) && !(current & SIGNAL_UNMASKABLE) &&
              !sigpending(&pending) && !pending,
          182);
  REQUIRE(sigaddset(&set, 5) == -1 && errno == EINVAL && !set, 183);
  errno = ENOENT;
}
static NORETURN void
trampoline(u64 signum, const Siginfo *info, void *context, u64 function, u64 address, u64 flags)
{
  Sighead *head = (Sighead *) address;
  u64      mode = frame_trace[1], mask, expected, old;
  Sigact   action;
  int      saved_errno = errno;
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
  errno = saved_errno;
  __sys_sigreturn((void *) address);
}
static void
install(u64 mode)
{
  Sigact   action, expected, old, bad;
  Allocarg allocation;
  Maparg   mapping;
  Handle   code = 0;
  u32      i;
  memset(&action, 0, sizeof(action));
  action.kind = SIGNAL_HANDLER;
  action.handler = frame_trace[2];
  action.trampoline = frame_trace[3];
  action.mask = ((u64) 1 << (SIGNAL_TERM - 1)) | SIGNAL_UNMASKABLE;
  action.flags =
      SIGNAL_SIGINFO | (mode == 8 ? SIGNAL_RESETHAND : 0) | (mode == 9 ? SIGNAL_NODEFER : 0);
  if (mode == 10) {
    allocation.bytes = PAGE_SIZE;
    allocation.kind = ALLOC_CODE;
    allocation.align = allocation.minaddr = allocation.maxaddr = allocation.reserved = 0;
    REQUIRE(!__sys_alloc(bootresource(sysboot(), BOOT_SELF_ALLOC), &allocation, &code) &&
                !__sys_seal(code),
            184);
    mapping.mem = code;
    mapping.offset = 0;
    mapping.address = 0x2000000;
    mapping.length = PAGE_SIZE;
    mapping.prot = MAP_READ | MAP_EXEC;
    mapping.reserved = 0;
    REQUIRE(!__sys_map(bootresource(sysboot(), BOOT_SELF_SPACE), &mapping), 185);
    action.handler = frame_trace[2] = mapping.address;
  }
  expected = action;
  if (!mode) {
    REQUIRE(!__sys_sigaction(SIGNAL_USR1, NULL, &old) && old.kind == SIGNAL_DEFAULT, 186);
    for (i = 0; i < 8; i++) {
      bad = action;
      if (i == 0)
        bad.handler = 0;
      if (i == 1)
        bad.trampoline = 1;
      if (i == 2)
        bad.handler = (u64) sysboot()->header;
      if (i == 3)
        bad.trampoline = 0x2000000;
      if (i == 4)
        bad.handler = BOOT_USER_HI;
      if (i == 5)
        bad.flags = 64;
      if (i == 6)
        bad.flags = SIGNAL_RESTART;
      if (i == 7)
        bad.reserved = 1;
      REQUIRE(__sys_sigaction(SIGNAL_USR1, &bad, NULL) == (i >= 2 && i <= 4 ? -ERR_FAULT
                                                           : i == 6         ? -ERR_NOTSUP
                                                                            : -ERR_INVALID),
              187);
      REQUIRE(!__sys_sigaction(SIGNAL_USR1, NULL, &old) && old.kind == SIGNAL_DEFAULT, 188);
    }
    REQUIRE(__sys_sigaction(SIGNAL_USR1, &action, (Sigact *) sysboot()->header) == -ERR_FAULT, 189);
    REQUIRE(!__sys_sigaction(SIGNAL_USR1, NULL, &old) && old.kind == SIGNAL_DEFAULT, 190);
  }
  /* Aliased input/output must install the snapshotted handler, while returning
     the old default action into the caller's original input object. */
  REQUIRE(!__sys_sigaction(SIGNAL_USR1, &action, &action) && action.kind == SIGNAL_DEFAULT, 191);
  REQUIRE(!__sys_sigaction(SIGNAL_USR1, NULL, &old) && old.kind == SIGNAL_HANDLER &&
              old.handler == expected.handler && old.trampoline == expected.trampoline &&
              old.mask == (expected.mask & ~SIGNAL_UNMASKABLE) && old.flags == expected.flags,
          192);
  if (!mode) {
    memset(&bad, 0, sizeof(bad));
    bad.kind = SIGNAL_IGNORE;
    REQUIRE(__sys_sigaction(SIGNAL_USR1, &bad, (Sigact *) sysboot()->header) == -ERR_FAULT, 193);
    REQUIRE(!__sys_sigaction(SIGNAL_USR1, NULL, &old) && old.kind == SIGNAL_HANDLER, 194);
  }
  expected.mask = (u64) 1 << (SIGNAL_PIPE - 1);
  expected.flags = SIGNAL_SIGINFO;
  REQUIRE(!__sys_sigaction(SIGNAL_USR2, &expected, NULL), 195);
  if (mode == 10) {
    REQUIRE(!__sys_free(code) && !__sys_unmap(bootresource(sysboot(), BOOT_SELF_SPACE), 0x2000000),
            196);
  }
}
static void
forcedhandler(u64 signum, const Siginfo *info, void *context)
{
  u64 mask, pending, first = frame_trace[12] == 0;
  UNUSED(context);
  REQUIRE(signum == SIGNAL_SEGV && info->signum == signum && !info->code && !info->pid &&
              !info->uid && !info->reserved0 && !info->reserved[0] && !info->reserved[1] &&
              info->address == (first ? 0xa011U : 0xb022U) &&
              info->reason == (first ? 0xa1U : 0xb2U) &&
              info->status == (first ? 0xf001U : 0xf002U),
          203);
  REQUIRE(!__sys_sigpending(&pending) && pending == (first ? (u64) 1 << (SIGNAL_SEGV - 1) : 0),
          204);
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, 0, &mask) &&
              mask == (1 | ((u64) 1 << (SIGNAL_TERM - 1)) | ((u64) 1 << (SIGNAL_SEGV - 1))),
          205);
  REQUIRE(frame_fp(first ? 2 : 3) == 1, 206);
  frame_trace[12]++;
  errno = ENOENT;
}
static NORETURN void
forcedtrampoline(u64 signum, const Siginfo *info, void *context, u64 function, u64 address,
                 u64 flags)
{
  Sighead *head = (Sighead *) address;
  int      saved_errno = errno;
  REQUIRE(function == (u64) forcedhandler && flags == SIGNAL_SIGINFO &&
              (u64) context == address + SIGNAL_FRAME_TF && (u64) info == address + 64 &&
              head->magic == SIGNAL_FRAME_MAGIC && head->depth == 1 && !head->previous &&
              head->restore_mask == 1 && !head->flags && !head->reserved && head->cookie,
          207);
  if (!frame_trace[12])
    frame_trace[6] = head->cookie;
  else {
    REQUIRE(head->cookie > frame_trace[6], 208);
    frame_trace[7] = head->cookie;
  }
  forcedhandler(signum, info, context);
  errno = saved_errno;
  __sys_sigreturn((void *) address);
}
static int
forceduser(void)
{
  Sigact action;
  u64    mask, pending;
  action.kind = SIGNAL_HANDLER;
  action.handler = (u64) forcedhandler;
  action.trampoline = (u64) forcedtrampoline;
  action.mask = (u64) 1 << (SIGNAL_TERM - 1);
  action.flags = SIGNAL_SIGINFO;
  action.reserved = 0;
  REQUIRE(!__sys_sigaction(SIGNAL_SEGV, &action, NULL) &&
              !__sys_sigmask(SIGNAL_SETMASK, 1, &mask) && !mask,
          209);
  errno = EDOM;
  REQUIRE(frame_fp(1) == 0, 210);
  frame_trace[0] = 1;
  while (frame_trace[12] < 2)
    REQUIRE(!__sys_yield(), 211);
  REQUIRE(frame_trace[12] == 2 && frame_fp(0) == 1 && errno == EDOM &&
              !__sys_sigmask(SIGNAL_BLOCK, 0, &mask) && mask == 1 && !__sys_sigpending(&pending) &&
              !pending,
          212);
  frame_trace[0] = 99;
  return 42;
}
static void
contexthandler(u64 signum)
{
  Rkey   nested;
  Sysret result;
  isize  status;
  REQUIRE(signum == SIGNAL_USR1 && runtime_contexts.contexts[0].phase == RCTX_DONE &&
              runtime_contexts.contexts[0].active == 1 &&
              runtime_contexts.contexts[0].result.values[0] == (u64) context_self.pid,
          213);
  REQUIRE(!sysctxprepare(&runtime_contexts, context_self, 1, SYS_GETPG, NULL, &nested) &&
              !sysctxinvoke(&runtime_contexts, context_self, nested) &&
              !sysctxresult(&runtime_contexts, context_self, nested, &status, &result) && !status &&
              result.values[0] == 1 && result.values[1] == 1 &&
              !sysctxfinish(&runtime_contexts, context_self, nested),
          214);
  REQUIRE(runtime_contexts.contexts[0].result.values[0] == (u64) context_self.pid &&
              frame_fp(2) == 1,
          215);
  frame_trace[20] = nested.sequence;
  frame_trace[21] = runtime_key.sequence;
  frame_trace[19] = 1;
  frame_trace[12]++;
  errno = ENOENT;
}
static NORETURN void
contexttrampoline(u64 signum, const Siginfo *info, void *context, u64 function, u64 address,
                  u64 flags)
{
  u64 mask, ignored;
  int saved_errno = errno;
  REQUIRE(signum == SIGNAL_USR1 && info->signum == signum && !info->code &&
              function == (u64) contexthandler && !flags &&
              runtime_contexts.contexts[0].phase == RCTX_WAITING,
          216);
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
              sysctxcapture(&runtime_contexts, context_self, runtime_key, context) == 1 &&
              runtime_contexts.contexts[0].phase == RCTX_DONE &&
              !runtime_contexts.contexts[0].status &&
              sysctxfinish(&runtime_contexts, context_self, runtime_key) == -ERR_BUSY,
          217);
  frame_trace[18] = 1;
  contexthandler(signum);
  errno = saved_errno;
  REQUIRE(!__sys_sigmask(SIGNAL_SETMASK, mask, &ignored), 218);
  __sys_sigreturn((void *) address);
}
static int
contextuser(void)
{
  Handle parent;
  Sigact action;
  Sysret result;
  isize  status;
  u64    mask, ignored;
  REQUIRE(!__sys_getid(&context_self.pid, &context_self.image, &parent) && parent == 1 &&
              !sysctxinit(&runtime_contexts, context_self),
          219);
  action.kind = SIGNAL_HANDLER;
  action.handler = (u64) contexthandler;
  action.trampoline = (u64) contexttrampoline;
  action.mask = action.flags = action.reserved = 0;
  REQUIRE(!__sys_sigaction(SIGNAL_USR1, &action, NULL) && frame_fp(1) == 0, 220);
  frame_trace[16] = (u64) (uintptr_t) __sys_ctxreturn_begin;
  frame_trace[17] = (u64) (uintptr_t) __sys_ctxresume;
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
              !sysctxprepare(&runtime_contexts, context_self, 0, SYS_GETID, NULL, &runtime_key) &&
              !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored),
          221);
  errno = EDOM;
  frame_trace[0] = 1;
  REQUIRE(!sysctxinvoke(&runtime_contexts, context_self, runtime_key) &&
              runtime_contexts.contexts[0].phase == RCTX_DONE &&
              !runtime_contexts.contexts[0].active && frame_trace[12] == 1 &&
              frame_trace[18] == 1 && frame_trace[19] == 1 && errno == EDOM && frame_fp(0) == 1,
          222);
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
              !sysctxresult(&runtime_contexts, context_self, runtime_key, &status, &result) &&
              !status && result.values[0] == (u64) context_self.pid &&
              result.values[1] == context_self.image && result.values[2] == 1 &&
              !result.values[3] && !result.values[4] && !result.values[5] && !result.values[6] &&
              !result.image && !sysctxfinish(&runtime_contexts, context_self, runtime_key) &&
              !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored),
          223);
  frame_trace[0] = 99;
  return 42;
}
static Forkret   forkcapture;
static Handle    forksource;
static u64       forkimage;
static Rkey      forkcompleted;
static Rkey      forkouter;
static const u64 forkbytes = sizeof(Fix) + FIX_LAYER_COUNT * sizeof(Fixlayer) + sizeof(u64);
static void
forkrepair(const Forkret *result)
{
  Fixview         view;
  const Fixlayer *layers;
  const Fixconn  *connections;
  u64             count;
  Connpeer        child;
  Handle          parent;
  u64             mask, ignored;
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
              !__sys_getid(&child.pid, &child.image, &parent) &&
              !sysfixchild(result, forkbytes, forksource, forkimage, &view) &&
              !sysfixlayers(&view, &layers) && !sysfixconnections(&view, &connections, &count) &&
              !count && !connections &&
              !sysctxfixraw(&runtime_contexts, context_self, child, layers) &&
              !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored),
          244);
}
static NORETURN void
forktrampoline(u64 signum, const Siginfo *info, void *context, u64 function, u64 address, u64 flags)
{
  UNUSED(info);
  UNUSED(function);
  UNUSED(flags);
  REQUIRE(signum == SIGNAL_USR1 && !forkcapture.pid &&
              sysforkdecode(context, (u64) __sys_forkreturn_begin, (u64) __sys_forkreturn_end,
                            &forkcapture) == 1,
          242);
  forkrepair(&forkcapture);
  __sys_sigreturn((void *) address);
}
static int
forkprepuser(void)
{
  Sysret    result;
  Forkret   forked;
  Createarg create = {128, 128, 16, 1, 0, 0, 0, 0, 0};
  const u64 address = 0x30000000;
  u64       args[7] = {0};
  Handle    authority = (Handle) frame_trace[18], child, construct;
  Handle    source, parent, published;
  u64       image;
  Elfwork   work;
  Sigact    action;
  struct
  {
    Fix      header;
    Fixlayer contexts[FIX_LAYER_COUNT];
    u64      connection_count;
  } fix;
  REQUIRE(__sys_forkprep(0, 0, &forked) == -ERR_BAD_HANDLE, 224);
  REQUIRE(__sys_forkprep(authority, 9, &forked) == -ERR_INVALID, 225);
  REQUIRE(!__sys_forkprep(authority, 2, &forked) && forked.construct && forked.pid && !forked.fix,
          226);
  construct = forked.construct;
  child = forked.pid;
  args[0] = construct;
  args[1] = 0;
  REQUIRE(__sys_enter(SYS_START, args, &result) == -ERR_STATE, 227);
  REQUIRE(!__sys_forkabort(construct), 228);
  args[0] = child;
  args[1] = 1;
  REQUIRE(__sys_enter(SYS_WAIT, args, &result) == -ERR_CHILD, 229);
  args[0] = authority;
  args[1] = (u64) &create;
  REQUIRE(!__sys_enter(SYS_CREATE, args, &result) && result.values[0], 230);
  args[0] = result.values[0];
  args[1] = 0;
  REQUIRE(__sys_forkabort((Handle) args[0]) == -ERR_STATE, 231);
  REQUIRE(!__sys_enter(SYS_DESTROY, args, &result), 232);
  REQUIRE(!__sys_getid(&source, &image, &parent), 241);
  forksource = source;
  forkimage = image;
  context_self.pid = source;
  context_self.image = image;
  REQUIRE((frame_trace[1] == 14 || !sysctxinit(&runtime_contexts, context_self)) &&
              !sysctxprepare(&runtime_contexts, context_self, 0, SYS_GETID, NULL, &runtime_key) &&
              !sysctxprepare(&runtime_contexts, context_self, 1, SYS_GETID, NULL, &forkcompleted) &&
              !sysctxinvoke(&runtime_contexts, context_self, forkcompleted),
          245);
  action.kind = SIGNAL_HANDLER;
  action.handler = (u64) forktrampoline;
  action.trampoline = (u64) forktrampoline;
  action.mask = action.flags = action.reserved = 0;
  REQUIRE(!__sys_sigaction(SIGNAL_USR1, &action, NULL), 243);
  frame_trace[16] = (u64) __sys_forkreturn_begin;
  frame_trace[0] = 1;
  REQUIRE(!__sys_forkprep(authority, 0, &forked), 233);
  if (!forked.construct) {
    Handle   self, ppid;
    u64      childimage;
    isize    status;
    Connpeer peer;
    forkrepair(&forked);
    REQUIRE(!__sys_getid(&self, &childimage, &ppid) && forked.pid == self &&
                (u64) forked.fix == address && ppid == source && forkcapture.pid == self &&
                forkcapture.fix == forked.fix && !forkcapture.construct,
            234);
    peer.pid = self;
    peer.image = childimage;
    REQUIRE(!sysctxresult(&runtime_contexts, peer, forkcompleted, &status, &result) && !status &&
                result.values[0] == (u64) source && result.values[1] == image &&
                !sysctxfinish(&runtime_contexts, peer, forkcompleted),
            247);
    REQUIRE(!sysctxinvoke(&runtime_contexts, peer, runtime_key) &&
                !sysctxresult(&runtime_contexts, peer, runtime_key, &status, &result) && !status &&
                result.values[0] == (u64) self && result.values[1] == childimage &&
                !sysctxfinish(&runtime_contexts, peer, runtime_key),
            246);
    return 43;
  }
  construct = forked.construct;
  child = forked.pid;
  args[0] = construct;
  REQUIRE(__sys_forkcommit(construct, (const void *) frame_trace, 64, &published) == -ERR_FAULT,
          235);
  memset(&fix, 0, sizeof(fix));
  fix.header.format = FIX_FORMAT;
  fix.header.bytes = sizeof(fix);
  fix.header.source_pid = source;
  fix.header.source_image = image;
  fix.header.target_pid = child;
  fix.header.contexts_off = sizeof(Fix);
  fix.contexts[0].sequence = runtime_key.sequence;
  fix.contexts[0].action = FIX_LAYER_UNSENT;
  fix.contexts[1].sequence = forkcompleted.sequence;
  fix.contexts[1].action = FIX_LAYER_COMPLETE;
  if (frame_trace[1] == 14) {
    fix.contexts[2].sequence = forkouter.sequence;
    fix.contexts[2].action = FIX_LAYER_INTERRUPT;
  }
  memset(&work, 0, sizeof(work));
  work.view = construct;
  work.pid = child;
  work.parent_space = bootresource(sysboot(), BOOT_SELF_SPACE);
  work.scratch = address;
  work.scratch_bytes = 4096;
  REQUIRE(!sysfixload(&work, address, &fix, sizeof(fix)) && !work.memory && !work.mapped, 237);
  REQUIRE(!__sys_forkcommit(construct, (const void *) address, sizeof(fix), &published) &&
              published == child,
          238);
  args[1] = args[2] = 0;
  {
    isize status = __sys_forkabort(construct);
    REQUIRE(status == -ERR_REVOKED || status == -ERR_BAD_HANDLE, 239);
  }
  args[0] = child;
  REQUIRE(!__sys_enter(SYS_WAIT, args, &result), 240);
  frame_trace[20] = result.values[1];
  frame_trace[21] = result.values[2];
  REQUIRE((Handle) result.values[0] == child && result.values[1] == WAIT_EXITED &&
              result.values[2] == 43,
          240);
  frame_trace[0] = 99;
  return 42;
}
static int handlerforkresult;
static NORETURN void
handlerforktrampoline(u64 signum, const Siginfo *info, void *context, u64 function, u64 address,
                      u64 flags)
{
  UNUSED(info);
  UNUSED(function);
  UNUSED(flags);
  REQUIRE(signum == SIGNAL_USR2, 248);
  handlerforkresult = forkprepuser();
  REQUIRE(sysctxcapture(&runtime_contexts, runtime_contexts.self, forkouter, context) == 1, 251);
  __sys_sigreturn((void *) address);
}
int
main(int argc, char **argv)
{
  u64 mode, old, mask;
  REQUIRE(argc == 1 && argv[0][0] >= '0' && argv[0][0] <= '>', 164);
  mode = (u64) (argv[0][0] - '0');
  frame_trace[1] = mode;
  if (mode == 11)
    return forceduser();
  if (mode == 12)
    return contextuser();
  if (mode == 13)
    return forkprepuser();
  if (mode == 14) {
    Sigact action;
    Handle self, parent;
    u64    image;
    isize  invoked, status;
    Sysret result;
    REQUIRE(!__sys_getid(&self, &image, &parent), 249);
    action.kind = SIGNAL_HANDLER;
    action.handler = action.trampoline = (u64) handlerforktrampoline;
    action.mask = action.flags = action.reserved = 0;
    context_self.pid = self;
    context_self.image = image;
    REQUIRE(!sysctxinit(&runtime_contexts, context_self) &&
                !sysctxprepare(&runtime_contexts, context_self, 2, SYS_GETID, NULL, &forkouter) &&
                !__sys_sigaction(SIGNAL_USR2, &action, NULL),
            250);
    frame_trace[16] = (u64) __sys_ctxreturn_begin;
    frame_trace[0] = 1;
    invoked = sysctxinvoke(&runtime_contexts, context_self, forkouter);
    REQUIRE(!sysctxresult(&runtime_contexts, runtime_contexts.self, forkouter, &status, &result) &&
                invoked == status &&
                (handlerforkresult == 43 ? status == -ERR_INTERRUPTED && !result.values[0]
                                         : !status && result.values[0] == (u64) self) &&
                !sysctxfinish(&runtime_contexts, runtime_contexts.self, forkouter) &&
                (handlerforkresult == 42 || handlerforkresult == 43),
            250);
    return handlerforkresult;
  }
  frame_trace[2] = (u64) (uintptr_t) handler;
  frame_trace[3] = (u64) (uintptr_t) trampoline;
  install(mode);
  errno = EDOM;
  REQUIRE(frame_fp(1) == 0, 165);
  REQUIRE(!__sys_sigmask(SIGNAL_SETMASK, 1, &old) && !old, 166);
  if (mode == 6)
    __sys_sigreturn((void *) 0x3ffffef000UL);
  frame_trace[0] = 1;
  if (!mode)
    REQUIRE(!frame_wait(), 197);
  while (!frame_trace[12])
    REQUIRE(!__sys_yield(), 167);
  if (mode == 4 || mode == 8) {
    REQUIRE(frame_fp(1) == 1, 168);
    frame_trace[0] = 3;
    for (;;)
      REQUIRE(!__sys_yield(), 169);
  }
  REQUIRE((mode == 0 || mode == 1 || mode == 9) && errno == EDOM, 170);
  REQUIRE(frame_fp(1) == 1, 171);
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, 0, &mask) &&
              mask == (mode == 0 ? ((u64) 1 << (SIGNAL_USR2 - 1)) : 1),
          172);
  REQUIRE(frame_trace[12] == (mode == 0 ? 1U : 2U), 173);
  frame_trace[0] = 99;
  return 42;
}
