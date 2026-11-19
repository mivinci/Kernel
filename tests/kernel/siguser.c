#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include "private/elf.h"
#include "private/start.h"
extern const u8       sig_child[], sig_child_end[];
volatile u64          signal_trace[24];
typedef struct Packet Packet;
struct Packet
{
  Boot     header;
  Strref   arg;
  Resentry resources[3];
  char     name[16];
};
static Packet  packet;
static Elfplan plan;
static u32     childruid;
#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      signal_trace[1] = (n);                                                                       \
      exit(n);                                                                                     \
    }                                                                                              \
  } while (0)
static isize
readchild(void *context, u64 offset, void *out, u64 bytes)
{
  UNUSED(context);
  memcpy(out, sig_child + offset, bytes);
  return 0;
}
static Handle
spawn(u32 index, const char *name, u64 defaults, u64 mask, u32 repeat)
{
  Elfsource source;
  Elfwork   work;
  Createarg args;
  Config    config;
  Handle    pid, control;
  u32       ids[4];
  source.context = NULL;
  source.bytes = (u64) (sig_child_end - sig_child);
  source.read = readchild;
  REQUIRE(!elfcheck(&source, 64, &plan), 160);
  memset(&work, 0, sizeof(work));
  memset(&args, 0, sizeof(args));
  REQUIRE(!__sys_getcred(0, 0, 0, ids) && !__sys_sigmask(SIGNAL_BLOCK, 0, &work.sigmask), 161);
  args.memlimit = 64;
  args.exec_limit = 128;
  args.resources = 3;
  args.pending = 1;
  args.ruid = ids[0];
  args.euid = ids[1];
  args.rgid = ids[2];
  args.egid = ids[3];
  REQUIRE(!__sys_create(bootresource(sysboot(), BOOT_CREATE), &args, &work.view, &pid, &control) &&
              !control,
          162);
  signal_trace[8 + index] = (u64) pid;
  work.pid = pid;
  work.image = 1;
  work.parent_space = bootresource(sysboot(), BOOT_SELF_SPACE);
  work.scratch = 0x2000000;
  work.scratch_bytes = BOOT_MAX_BYTES;
  memset(&packet, 0, sizeof(packet));
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.argc = 1;
  packet.header.argv_off = offsetof(Packet, arg);
  packet.header.resource_count = 3;
  packet.header.resources_off = offsetof(Packet, resources);
  packet.header.user_lo = BOOT_USER_LO;
  packet.header.user_hi = BOOT_USER_HI;
  packet.arg.offset = offsetof(Packet, name);
  packet.arg.length = (u32) strlen(name) + 1;
  memcpy(packet.name, name, packet.arg.length);
  packet.resources[0].tag = BOOT_SELF_SPACE;
  packet.resources[1].tag = BOOT_SELF_ALLOC;
  packet.resources[2].tag = BOOT_CREATE;
  REQUIRE(!elfload(&source, &plan, &packet, sizeof(packet), &work, &config), 163);
  config.sigmask = mask;
  config.sigdefault = defaults;
  REQUIRE(!__sys_configure(work.view, &config, 0), 164);
  if (repeat) {
    config.sigmask = (u64) 1 << 4;
    REQUIRE(__sys_configure(work.view, &config, 0) == -ERR_INVALID, 165);
    config.sigmask = mask;
    config.sigdefault = 0;
    REQUIRE(!__sys_configure(work.view, &config, 0), 166);
  }
  REQUIRE(!__sys_start(work.view), 167);
  return pid;
}
static void
setchild(u64 kind, u64 flags)
{
  Sigact action;
  memset(&action, 0, sizeof(action));
  action.kind = kind;
  action.flags = flags;
  REQUIRE(!__sys_sigaction(SIGNAL_CHLD, &action, NULL), 168);
}
static void
childhandler(u64 signum, const Siginfo *info)
{
  REQUIRE(signum == SIGNAL_CHLD && info->signum == SIGNAL_CHLD &&
              info->code == SIGNAL_SOURCE_CHILD && info->pid == signal_trace[8] &&
              info->uid == childruid && !info->reserved0 && !info->address &&
              info->reason == WAIT_EXITED && info->status == 31 && !info->reserved[0] &&
              !info->reserved[1],
          200);
  signal_trace[16]++;
  signal_trace[17] = info->pid;
  signal_trace[18] = info->uid;
  signal_trace[19] = info->code;
  signal_trace[20] = info->reason;
  signal_trace[21] = info->status;
}
static NORETURN void
childtrampoline(u64 signum, const Siginfo *info, void *context, u64 function, u64 address,
                u64 flags)
{
  REQUIRE(function == (u64) childhandler && flags == SIGNAL_SIGINFO && (u64) info == address + 64 &&
              (u64) context == address + 128,
          201);
  childhandler(signum, info);
  __sys_sigreturn((void *) address);
}
int
main(int argc, char **argv)
{
  Sigact action, old, snapshot;
  Handle pid;
  u64    before, current, pending, usr1 = (u64) 1 << (SIGNAL_USR1 - 1),
                                usr2 = (u64) 1 << (SIGNAL_USR2 - 1);
  int status;
  u32 i;
  u32 ids[4];
  REQUIRE(argc == 0 && argv && !argv[0], 169);
  signal_trace[0] = 1;
  REQUIRE(!__sys_sigmask(SIGNAL_SETMASK, usr1 | SIGNAL_UNMASKABLE, &before) && !before, 170);
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, usr2, &before) && before == usr1, 171);
  REQUIRE(!__sys_sigmask(SIGNAL_UNBLOCK, usr2, &before) && before == (usr1 | usr2), 172);
  REQUIRE(__sys_sigmask(3, 0, &before) == -ERR_INVALID && !before, 173);
  REQUIRE(__sys_sigmask(SIGNAL_SETMASK, (u64) 1 << 4, &before) == -ERR_INVALID && !before, 174);
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, 0, &current) && current == usr1, 175);
  REQUIRE(!__sys_sigpending(&pending) && !pending, 176);
  memset(&action, 0, sizeof(action));
  action.kind = SIGNAL_IGNORE;
  action.mask = usr2 | SIGNAL_UNMASKABLE;
  REQUIRE(!__sys_sigaction(SIGNAL_TERM, &action, &old) && old.kind == SIGNAL_DEFAULT, 177);
  REQUIRE(!__sys_sigaction(SIGNAL_TERM, NULL, &snapshot) && snapshot.kind == SIGNAL_IGNORE &&
              snapshot.mask == usr2,
          178);
  action.kind = SIGNAL_DEFAULT;
  action.mask = 0;
  REQUIRE(__sys_sigaction(SIGNAL_TERM, &action, (Sigact *) sysboot()->header) == -ERR_FAULT, 179);
  REQUIRE(!__sys_sigaction(SIGNAL_TERM, NULL, &snapshot) && snapshot.kind == SIGNAL_IGNORE, 180);
  REQUIRE(!__sys_sigaction(SIGNAL_TERM, &action, &action) && action.kind == SIGNAL_IGNORE, 181);
  REQUIRE(!__sys_sigaction(SIGNAL_TERM, NULL, &snapshot) && snapshot.kind == SIGNAL_DEFAULT, 182);
  memset(&action, 0, sizeof(action));
  action.kind = SIGNAL_IGNORE;
  REQUIRE(!__sys_sigaction(SIGNAL_TERM, &action, NULL), 183);
  for (i = 0; i < 3; i++) {
    u64 number = i == 0 ? 0 : i == 1 ? 5 : ((u64) 1 << 32) | SIGNAL_TERM;
    REQUIRE(__sys_sigaction(number, &action, NULL) == -ERR_INVALID, 184);
  }
  REQUIRE(__sys_sigaction(SIGNAL_KILL, &action, NULL) == -ERR_INVALID, 185);
  REQUIRE(__sys_sigaction(SIGNAL_STOP, &action, NULL) == -ERR_INVALID, 186);
  action.flags = 64;
  REQUIRE(__sys_sigaction(SIGNAL_TERM, &action, NULL) == -ERR_INVALID, 187);
  action.flags = SIGNAL_RESTART;
  REQUIRE(__sys_sigaction(SIGNAL_TERM, &action, NULL) == -ERR_NOTSUP, 188);
  action.flags = 0;
  action.kind = SIGNAL_HANDLER;
  REQUIRE(__sys_sigaction(SIGNAL_TERM, &action, NULL) == -ERR_INVALID, 189);
  REQUIRE(__sys_sigaction(SIGNAL_TERM, (const Sigact *) 1, NULL) == -ERR_FAULT, 190);
  memset(&action, 0, sizeof(action));
  action.flags = SIGNAL_NOCLDWAIT;
  REQUIRE(__sys_sigaction(SIGNAL_CHLD, &action, (Sigact *) sysboot()->header) == -ERR_FAULT, 191);
  REQUIRE(!__sys_sigaction(SIGNAL_CHLD, NULL, &snapshot) && snapshot.kind == SIGNAL_DEFAULT &&
              !snapshot.flags,
          192);
  signal_trace[5] = 1;
  REQUIRE(!__sys_getcred(0, 0, 0, ids), 202);
  childruid = ids[0];
  memset(&action, 0, sizeof(action));
  action.kind = SIGNAL_HANDLER;
  action.handler = (u64) childhandler;
  action.trampoline = (u64) childtrampoline;
  action.flags = SIGNAL_SIGINFO;
  REQUIRE(!__sys_sigaction(SIGNAL_CHLD, &action, NULL), 203);
  pid = spawn(0, "keep", 0, usr1, 0);
  REQUIRE(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 31, 193);
  REQUIRE(signal_trace[16] == 1 && signal_trace[17] == (u64) pid && signal_trace[18] == childruid &&
              signal_trace[19] == SIGNAL_SOURCE_CHILD && signal_trace[20] == WAIT_EXITED &&
              signal_trace[21] == 31,
          204);
  setchild(SIGNAL_DEFAULT, 0);
  signal_trace[3]++;
  pid = spawn(1, "keep", 0, usr1, 0);
  REQUIRE(!__sys_yield(), 194);
  setchild(SIGNAL_IGNORE, 0);
  REQUIRE(waitpid(pid, &status, 0) == pid && WEXITSTATUS(status) == 31, 195);
  signal_trace[3]++;
  signal_trace[14] = 1;
  for (i = 2; i < 6; i++) {
    if (i == 3)
      setchild(SIGNAL_DEFAULT, SIGNAL_NOCLDWAIT);
    else
      setchild(SIGNAL_IGNORE, 0);
    signal_trace[2] = i;
    pid = spawn(i,
                i == 2   ? "auto"
                : i == 3 ? "keep"
                : i == 4 ? "reset"
                         : "again",
                i >= 4 ? ((u64) 1 << (SIGNAL_TERM - 1)) | ((u64) 1 << (SIGNAL_CHLD - 1)) : 0,
                i == 4 ? usr2 : usr1, i == 5);
    status = 0x12345678;
    REQUIRE(waitpid(pid, &status, WNOHANG) == 0 && status == 0x12345678, 196);
    REQUIRE(waitpid(pid, &status, 0) == -1 && errno == ECHILD && status == 0x12345678, 197);
    signal_trace[4]++;
  }
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, 0, &current) && current == usr1, 198);
  signal_trace[7] = current;
  REQUIRE(!__sys_sigpending(&pending) && !pending, 199);
  signal_trace[0] = 99;
  return 42;
}
