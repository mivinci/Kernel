#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "private/syscall.h"
#include "private/start.h"
#include "private/context.h"
#include "private/contextipc.h"
#include "private/sigentry.h"
#include "private/forkstate.h"
#include "private/fix.h"
#include "private/fork.h"
#include <abi/resource.h>
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
        bad.flags = 128;
      if (i == 7)
        bad.reserved = 1;
      REQUIRE(__sys_sigaction(SIGNAL_USR1, &bad, NULL) ==
                  (i >= 2 && i <= 4 ? -ERR_FAULT : -ERR_INVALID),
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
contexthandler(int signum)
{
  Rkey   nested;
  Sysret result;
  isize  status;
  REQUIRE(signum == SIGNAL_USR1 && runtime_contexts.contexts[0].phase == RCTX_DONE &&
              runtime_contexts.contexts[0].active == 1 &&
              runtime_contexts.contexts[0].result.values[0] == (u64) context_self.pid &&
              frame_trace[18] == 1 && errno == EDOM &&
              sysctxfinish(&runtime_contexts, context_self, runtime_key) == -ERR_BUSY,
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
static void
contextnested(int signum)
{
  Rkey     nested;
  Sysret   result;
  isize    status;
  sigset_t mask;
  REQUIRE(signum == SIGUSR2 && runtime_contexts.contexts[0].phase == RCTX_DONE &&
              runtime_contexts.contexts[0].active == 1 &&
              runtime_contexts.contexts[0].result.values[0] == (u64) context_self.pid &&
              errno == EDOM && !frame_trace[12] && !frame_trace[18],
          216);
  REQUIRE(!sigprocmask(SIG_BLOCK, NULL, &mask) &&
              mask == (((u64) 1 << (SIGUSR1 - 1)) | ((u64) 1 << (SIGUSR2 - 1))) &&
              !sysctxprepare(&runtime_contexts, context_self, 2, SYS_GETID, NULL, &nested) &&
              !sysctxinvoke(&runtime_contexts, context_self, nested) &&
              !sysctxresult(&runtime_contexts, context_self, nested, &status, &result) && !status &&
              result.values[0] == (u64) context_self.pid &&
              !sysctxfinish(&runtime_contexts, context_self, nested),
          217);
  frame_trace[18] = 1;
  errno = EINVAL;
}
static int
contextuser(void)
{
  struct sigaction action = {0};
  Handle           parent;
  Sigact           native;
  Sysret           result;
  isize            status;
  u64              mask, ignored;
  REQUIRE(!__sys_getid(&context_self.pid, &context_self.image, &parent) && parent == 1 &&
              !sysctxinit(&runtime_contexts, context_self) && !sysctxsigbind(&runtime_contexts),
          219);
  action.sa_handler = contexthandler;
  REQUIRE(!sigaction(SIGUSR1, &action, NULL), 220);
  action.sa_handler = contextnested;
  REQUIRE(!sigaction(SIGUSR2, &action, NULL) && !__sys_sigaction(SIGNAL_USR1, NULL, &native) &&
              frame_fp(1) == 0,
          220);
  frame_trace[16] = (u64) (uintptr_t) __sys_ctxreturn_begin;
  frame_trace[17] = (u64) (uintptr_t) __sys_ctxresume;
  frame_trace[22] = native.trampoline;
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
static Bootstrap forkboot;
static Connclaim forkclaim;
static Conncache forkdirectory;
static Conntag   forkoldtag;
static Fdtable   forkfds;
static Filepool  forkfiles;
static Fdtag     forkreserved;
static Filestage forkstage;
static Forkdone  forkdone;
static void
forkrepair(const Forkret *result)
{
  Fixview         view;
  const Fixlayer *layers;
  const Fixconn  *connections;
  u64             count;
  Connpeer        child;
  Connentry       entry;
  Handle          parent;
  u64             mask, ignored;
  Forkstate       state;
  state.fds = &forkfds;
  state.files = &forkfiles;
  state.connections = &forkdirectory;
  state.contexts = &runtime_contexts;
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
              !__sys_getid(&child.pid, &child.image, &parent) &&
              !sysfixchild(result, FIX_BASE_BYTES, forksource, forkimage, &view) &&
              !sysfixlayers(&view, &layers) && !sysfixconnections(&view, &connections, &count) &&
              !count && !connections,
          244);
  if (!forkdone.child.pid) {
    Fdview reserved;
    REQUIRE(
        sysforkrepair(&forkdone, &state, context_self, result, FIX_BASE_BYTES, NULL, 0, NULL) ==
                -ERR_NOTSUP &&
            !forkdone.child.pid &&
            !sysforkstatecheck(&state, context_self, child, layers, NULL, 0, connections, count) &&
            !sysfdobserve(&forkfds, forkreserved.index, &reserved) &&
            reserved.tag.state == SYSFD_RESERVED &&
            reserved.tag.generation == forkreserved.generation &&
            !sysfilerefstaged(&forkfiles, context_self, &forkstage),
        254);
    /* These fixture reserves have never been submitted to a service. */
    REQUIRE(
        !sysfdcancel(&forkfds, forkreserved) &&
            sysforkstateapply(&state, context_self, child, layers, NULL, 0, connections, count) ==
                -ERR_NOTSUP &&
            !sysforkstatecheck(&state, context_self, child, layers, NULL, 0, connections, count) &&
            !sysfilerefstaged(&forkfiles, context_self, &forkstage) &&
            !sysfilerefabort(&forkfiles, context_self, &forkstage),
        254);
  } else {
    Forkret wrong = *result;
    wrong.fix = (const void *) ((u64) result->fix + 8);
    REQUIRE(sysforkrepair(&forkdone, &state, context_self, &wrong, FIX_BASE_BYTES, NULL, 0, NULL) ==
                -ERR_STALE,
            244);
  }
  REQUIRE(!sysforkrepair(&forkdone, &state, context_self, result, FIX_BASE_BYTES, NULL, 0, NULL) &&
              forkdirectory.self.pid == child.pid && forkfiles.self.pid == child.pid &&
              runtime_contexts.self.pid == child.pid && forkdirectory.self.image == child.image &&
              forkfiles.self.image == child.image && runtime_contexts.self.image == child.image &&
              !sysfdcheck(&forkfds) && !sysfilerefcheck(&forkfiles, child) &&
              !sysconncheck(&forkdirectory, child) &&
              sysconnview(&forkdirectory, forkoldtag, &entry) == -ERR_STALE &&
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
static void
execprepuser(void)
{
  Handle   space = bootresource(sysboot(), BOOT_SELF_SPACE), view, rejected, data, self, parent;
  u64      number, oldimage, ignored, written, next;
  Allocarg alloc = {4096, ALLOC_DATA, 0, 0, 0, 0};
  Maparg   map = {0, 0, 0x30000000, 4096, MAP_READ | MAP_WRITE, 0};
  Queryrec records[2];
  isize    status;
  u32      i;
  REQUIRE(!__sys_getid(&self, &oldimage, &parent) &&
              __sys_execprep(0, 0, &rejected, &ignored) == -ERR_BAD_HANDLE && !rejected &&
              !ignored && __sys_execprep(space, 9, &rejected, &ignored) == -ERR_INVALID,
          191);
  REQUIRE(!__sys_execprep(space, 2, &view, &number) && view && number > oldimage &&
              __sys_execprep(space, 0, &rejected, &ignored) == -ERR_BUSY && !rejected && !ignored,
          192);
  REQUIRE(!__sys_query(view, QUERY_RESOURCE, 0, records, 2, &written, &next) && written == 2 &&
              records[0].image == number && records[1].image == number &&
              records[0].data[1] == RESOURCE_SPACE && records[1].data[1] == RESOURCE_ALLOC &&
              __sys_start(view) == -ERR_BAD_HANDLE && __sys_execabort(space) == -ERR_BAD_HANDLE,
          193);
  REQUIRE(!__sys_alloc(view, &alloc, &data), 194);
  map.mem = data;
  REQUIRE(!__sys_map(space, &map), 195);
  *(volatile u64 *) map.address = 0x31415926;
  map.address = 0x10000;
  REQUIRE(!__sys_map(view, &map) && !__sys_execabort(view) &&
              __sys_execabort(view) == -ERR_BAD_HANDLE &&
              __sys_query(view, QUERY_RESOURCE, 0, records, 2, &written, &next) ==
                  -ERR_BAD_HANDLE &&
              !__sys_unmap(space, 0x30000000),
          196);
  for (i = 0; i < 96; i++) {
    status = __sys_execprep(space, 0, &rejected, &ignored);
    if (status != -ERR_BUSY)
      break;
    REQUIRE(!__sys_yield(), 197);
  }
  REQUIRE(i < 96 && !status && rejected != view && ignored > number && !__sys_execabort(rejected) &&
              !__sys_getid(&self, &number, &parent) && number == oldimage,
          198);
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
  u64       fix[FIX_BASE_BYTES / sizeof(u64)];
  Forkfix   finish = {0};
  REQUIRE(__sys_forkprep(0, 0, &forked) == -ERR_BAD_HANDLE, 224);
  REQUIRE(__sys_forkprep(authority, 9, &forked) == -ERR_INVALID, 225);
  REQUIRE(!__sys_forkprep(authority, 2, &forked) && forked.construct && forked.pid && !forked.fix,
          226);
  construct = forked.construct;
  child = forked.pid;
  args[0] = construct;
  args[1] = 0;
  REQUIRE(__sys_enter(SYS_START, args, &result) == -ERR_STATE, 227);
  memset(&work, 0, sizeof(work));
  work.view = construct;
  work.pid = child;
  work.parent_space = bootresource(sysboot(), BOOT_SELF_SPACE);
  work.scratch = address;
  work.scratch_bytes = 4096;
  memset(fix, 0, sizeof(fix));
  REQUIRE(sysforkpublish(&work, address, fix, sizeof(fix)) == -ERR_INVALID &&
              work.failure == -ERR_INVALID && !work.view && !work.memory && !work.mapped,
          228);
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
  /* Identity-only directory fixture; no transport or service authority. */
  sysfdinit(&forkfds);
  REQUIRE(!sysconninit(&forkdirectory, context_self) &&
              !sysconnbind(&forkdirectory, context_self, &forkoldtag) &&
              !sysfilerefinit(&forkfiles, context_self) && !forkdone.child.pid &&
              !sysfdreserve(&forkfds, 3, &forkreserved) &&
              !sysfilerefreserve(&forkfiles, context_self, 1, &forkstage),
          252);
  REQUIRE((frame_trace[1] >= 14 || !sysctxinit(&runtime_contexts, context_self)) &&
              !sysctxprepare(&runtime_contexts, context_self, 0, SYS_GETID, NULL, &runtime_key) &&
              !sysctxprepare(&runtime_contexts, context_self, 1, SYS_GETID, NULL, &forkcompleted) &&
              !sysctxinvoke(&runtime_contexts, context_self, forkcompleted),
          245);
  REQUIRE(!__sys_forkprep(authority, 0, &forked) && forked.construct && !forked.fix, 237);
  work.view = forked.construct;
  work.pid = forked.pid;
  work.failure = 0;
  finish.address = address;
  finish.buffer = fix;
  finish.capacity = sizeof(fix) - 1;
  REQUIRE(sysforkfinish(&work, &runtime_contexts, context_self, &finish) == -ERR_CAPACITY &&
              work.failure == -ERR_CAPACITY && !work.view && !work.memory && !work.mapped,
          237);
  args[0] = forked.pid;
  args[1] = 1;
  REQUIRE(__sys_enter(SYS_WAIT, args, &result) == -ERR_CHILD, 237);
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
                forkdone.child.pid == self && forkdone.child.image == childimage &&
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
  memset(&work, 0, sizeof(work));
  work.view = construct;
  work.pid = child;
  work.parent_space = bootresource(sysboot(), BOOT_SELF_SPACE);
  work.scratch = address;
  work.scratch_bytes = 4096;
  finish.capacity = sizeof(fix);
  REQUIRE(sysforkfinish(&work, &runtime_contexts, context_self, &finish) == child && !work.view &&
              !work.memory && !work.mapped,
          238);
  args[1] = args[2] = 0;
  {
    isize status = __sys_forkabort(construct);
    REQUIRE(status == -ERR_REVOKED || status == -ERR_BAD_HANDLE, 239);
  }
  args[0] = child;
  REQUIRE(!__sys_enter(SYS_WAIT, args, &result), 240);
  {
    Connentry entry;
    Fdview    reserved;
    REQUIRE(!sysconnview(&forkdirectory, forkoldtag, &entry) && entry.state == SYSCONN_UNCLAIMED &&
                !sysconncheck(&forkdirectory, context_self) &&
                !sysfilerefcheck(&forkfiles, context_self) && !forkdone.child.pid &&
                !sysfdobserve(&forkfds, forkreserved.index, &reserved) &&
                reserved.tag.state == SYSFD_RESERVED &&
                reserved.tag.generation == forkreserved.generation &&
                !sysfilerefstaged(&forkfiles, context_self, &forkstage),
            253);
    REQUIRE(!sysfdcancel(&forkfds, forkreserved) &&
                !sysfilerefabort(&forkfiles, context_self, &forkstage),
            254);
  }
  frame_trace[20] = result.values[1];
  frame_trace[21] = result.values[2];
  REQUIRE((Handle) result.values[0] == child && result.values[1] == WAIT_EXITED &&
              result.values[2] == 43,
          240);
  frame_trace[28] = 1;
  frame_trace[29] = 1;
  frame_trace[0] = 99;
  return 42;
}
static int handlerforkresult;
#define FORK_FIX_ADDRESS 0x30000000UL
static void
publicforkhandler(int signum)
{
  Connpeer self;
  Handle   parent;
  u64      probe;
  REQUIRE(!__sys_getid(&self.pid, &self.image, &parent) && runtime_contexts.self.pid == self.pid &&
              runtime_contexts.self.image == self.image && !sysfilerefcheck(&forkfiles, self) &&
              !sysconncheck(&forkdirectory, self),
          266);
  if (self.pid == context_self.pid) {
    REQUIRE(signum == SIGUSR1 && parent == 1, 267);
    frame_trace[23]++;
  } else {
    REQUIRE(parent == context_self.pid && self.pid != context_self.pid && !sysfdcheck(&forkfds) &&
                __sys_copy(&probe, (const void *) FORK_FIX_ADDRESS, sizeof(probe)) == -ERR_FAULT &&
                signum == (frame_trace[1] == 17 && !frame_trace[24] ? SIGUSR2 : SIGUSR1),
            268);
    frame_trace[24]++;
  }
}
static int
publicforkuser(void)
{
  const u64  address = FORK_FIX_ADDRESS;
  u64        fix[FIX_BASE_BYTES / sizeof(u64)];
  Forkresume plan = {
      {&forkfds, &forkfiles, &forkdirectory, &runtime_contexts}, sizeof(fix), NULL, 0, NULL, 0};
  Forkfix          finish = {0};
  struct sigaction action = {0};
  Forkret          result;
  Sysret           tuple;
  Elfwork          work = {0};
  Handle           parent;
  isize            status;
  Sigact           native;
  u64              args[7] = {0}, mask, old;
  REQUIRE(!__sys_getid(&context_self.pid, &context_self.image, &parent) && parent == 1 &&
              !sysctxinit(&runtime_contexts, context_self) &&
              !sysfilerefinit(&forkfiles, context_self) &&
              !sysconninit(&forkdirectory, context_self),
          269);
  sysfdinit(&forkfds);
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
              !sysctxprepare(&runtime_contexts, context_self, 0, SYS_GETID, NULL, &runtime_key) &&
              !__sys_sigmask(SIGNAL_SETMASK, mask, &old),
          270);
  action.sa_handler = publicforkhandler;
  REQUIRE(!sigaction(SIGUSR1, &action, NULL) && !sigaction(SIGUSR2, &action, NULL) &&
              !__sys_sigaction(SIGUSR1, NULL, &native),
          271);
  frame_trace[16] = (u64) __sys_forkreturn_begin;
  frame_trace[17] = (u64) __sys_forkreturn_end;
  frame_trace[22] = native.trampoline;
  frame_trace[26] = 1;
  frame_trace[0] = 1;
  plan.space = bootresource(sysboot(), BOOT_SELF_SPACE);
  REQUIRE(sysforkresume(0, 0, &plan, &result) == -ERR_BAD_HANDLE && frame_trace[23] == 1 &&
              !result.construct && !result.pid && !result.fix,
          272);
  frame_trace[26] = 2;
  work.parent_space = plan.space;
  work.scratch = finish.address = address;
  work.scratch_bytes = 4096;
  finish.buffer = fix;
  finish.capacity = sizeof(fix);
  status = sysforklocal((Handle) frame_trace[18], 0, &plan, &work, &finish);
  REQUIRE(status >= 0, 273);
  if (!status) {
    Connpeer self = runtime_contexts.self;
    Handle   pid, ppid;
    u64      image;
    u64      probe;
    Elfwork  rejected = work;
    Forkfix  shortfix = finish;
    Fdtag    reserved;
    REQUIRE(frame_trace[24] == (frame_trace[1] == 17 ? 2U : 1U) && self.pid != context_self.pid &&
                !work.view && !work.pid &&
                __sys_copy(&probe, (const void *) address, sizeof(probe)) == -ERR_FAULT &&
                !sysctxinvoke(&runtime_contexts, self, runtime_key) &&
                !sysctxresult(&runtime_contexts, self, runtime_key, &status, &tuple) && !status &&
                tuple.values[0] == (u64) self.pid && tuple.values[1] == self.image &&
                !sysctxfinish(&runtime_contexts, self, runtime_key),
            274);
    REQUIRE(!sysfdreserve(&forkfds, 0, &reserved) &&
                sysforklocal((Handle) frame_trace[18], 0, &plan, &work, &finish) == -ERR_NOTSUP &&
                !work.view && !work.pid && !sysfdcancel(&forkfds, reserved),
            281);
    shortfix.capacity--;
    REQUIRE(sysforklocal((Handle) frame_trace[18], 0, &plan, &rejected, &shortfix) ==
                    -ERR_CAPACITY &&
                rejected.pid && !rejected.view && !rejected.memory && !rejected.mapped,
            282);
    args[0] = (u64) rejected.pid;
    REQUIRE(__sys_enter(SYS_WAIT, args, &tuple) == -ERR_CHILD, 283);
    status = sysforklocal((Handle) frame_trace[18], 0, &plan, &work, &finish);
    REQUIRE(status >= 0 && !work.view, 278);
    if (!status) {
      REQUIRE(!work.pid && !__sys_getid(&pid, &image, &ppid) && ppid == self.pid &&
                  pid != self.pid && pid == runtime_contexts.self.pid &&
                  image == runtime_contexts.self.image &&
                  __sys_copy(&probe, (const void *) address, sizeof(probe)) == -ERR_FAULT,
              279);
      return 44;
    }
    args[0] = (u64) status;
    REQUIRE(!__sys_enter(SYS_WAIT, args, &tuple) && tuple.values[0] == (u64) status &&
                tuple.values[1] == WAIT_EXITED && tuple.values[2] == 44,
            280);
    return 43;
  }
  REQUIRE(frame_trace[23] == 2 && !frame_trace[24] && !work.view && work.pid == status &&
              runtime_contexts.self.pid == context_self.pid,
          275);
  args[0] = (u64) status;
  REQUIRE(!__sys_enter(SYS_WAIT, args, &tuple) && tuple.values[0] == (u64) status &&
              tuple.values[1] == WAIT_EXITED && tuple.values[2] == 43,
          277);
  frame_trace[0] = 99;
  return 42;
}
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
  REQUIRE(argc == 1 && argv[0][0] >= '0' && argv[0][0] <= 'B', 164);
  mode = (u64) (argv[0][0] - '0');
  frame_trace[1] = mode;
  if (mode >= 17)
    return publicforkuser();
  if (mode == 16) {
    Ipcmsg request, event;
    Msg    reply = {{77, 1, 0, 0}};
    REQUIRE(!ipc_recv(0, 0, &request) && request.kind == IPC_CALL && request.words[3] == 701 &&
                !ipc_defer(request.request) && !ipc_recv((u64) 1 << 32, RECV_EVENTS_ONLY, &event) &&
                event.kind == IPC_EVENT && !ipc_reply(request.request, &reply),
            259);
    return 44;
  }
  if (mode == 11)
    return forceduser();
  if (mode == 12)
    return contextuser();
  if (mode == 13) {
    Handle view;
    u64    image;
    int    result;
    execprepuser();
    result = forkprepuser();
    REQUIRE(!__sys_execprep(bootresource(sysboot(), BOOT_SELF_SPACE), 0, &view, &image) && view &&
                image > 1,
            199);
    return result;
  }
  if (mode == 14 || mode == 15) {
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
                !__sys_sigaction(SIGNAL_USR2, &action, NULL),
            250);
    if (mode == 15) {
      Ipcmsg message;
      Msg    request = {{FS_ATTACH, 0, 1, 701}};
      Handle rpc;
      forkclaim.tag.generation = 1;
      forkclaim.tag.index = forkclaim.tag.reserved = 0;
      forkclaim.seq = 1;
      forkclaim.owner = 901;
      sysbootinit(&forkboot);
      REQUIRE(
          !sysbootprepare(&forkboot, forkclaim, FS_ATTACH, 701, 0) &&
              !ipc_call((Handle) frame_trace[22], &request, CALL_ASYNC, &message) &&
              (rpc = message.request) > 0 && !sysbootsubmitted(&forkboot, forkclaim, &rpc) &&
              !sysctxbootwait(&runtime_contexts, context_self, 2, &forkboot, forkclaim, &forkouter),
          254);
    } else
      REQUIRE(!sysctxprepare(&runtime_contexts, context_self, 2, SYS_GETID, NULL, &forkouter), 250);
    frame_trace[16] = (u64) __sys_ctxreturn_begin;
    frame_trace[0] = mode == 15 ? 4 : 1;
    invoked = sysctxinvoke(&runtime_contexts, context_self, forkouter);
    if (mode == 15) {
      Ipcmsg message;
      frame_trace[23] = (u64) invoked;
      frame_trace[24] = forkboot.state;
      frame_trace[25] = (u64) forkboot.rpc;
      REQUIRE(invoked == -ERR_INTERRUPTED && forkboot.state == SYSBOOT_WAITING && forkboot.rpc,
              255);
      if (handlerforkresult == 43) {
        REQUIRE(sysctxforkresult(&runtime_contexts, runtime_contexts.self, forkouter, &status) ==
                        1 &&
                    status == -ERR_INTERRUPTED &&
                    ipc_rqpoll(forkboot.rpc, 0, &message) == -ERR_BAD_HANDLE,
                256);
      } else {
        REQUIRE(handlerforkresult == 42, 257);
        REQUIRE(!sysctxhandoff(&runtime_contexts, runtime_contexts.self, forkouter), 260);
        status = ipc_notify((Handle) frame_trace[26], (u64) 1 << 32);
        frame_trace[27] = (u64) status;
        REQUIRE(!status, 261);
        status = ipc_rqwait(forkboot.rpc, 0, &message);
        frame_trace[27] = (u64) status;
        REQUIRE(!status, 262);
        REQUIRE(!sysbootcapture(&forkboot, forkclaim, 0, &message), 263);
        REQUIRE(forkboot.words[0] == 77, 264);
        REQUIRE(!sysbootack(&forkboot, forkclaim) && !sysbootfinish(&forkboot, forkclaim), 265);
      }
      REQUIRE(!sysctxfinish(&runtime_contexts, runtime_contexts.self, forkouter), 258);
      frame_trace[0] = 99;
      return handlerforkresult;
    }
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
