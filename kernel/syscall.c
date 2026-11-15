#include <syscall.h>
#include <proc.h>
#include <abi/syscall.h>
#include <diag.h>
#include <clock.h>

typedef union Arguments Arguments;
union Arguments
{
  Allocarg alloc;
  Maparg   map;
};
static int
known(u64 nr)
{
  return (nr >= 1 && nr <= 3) || (nr >= 0x10 && nr <= 0x1c) || (nr >= 0x20 && nr <= 0x25) ||
         (nr >= 0x30 && nr <= 0x36) || (nr >= 0x40 && nr <= 0x56) || (nr >= 0x60 && nr <= 0x6e) ||
         (nr >= 0x70 && nr <= 0x75);
}
NORETURN void
syscall(Hctx *context)
{
  Rtable   *table = proccurrent(context);
  Htrap     trap;
  Lifefault fault;
  Arguments snapshot;
  u64       args[HAL_SYSCALL_ARGS], outputs[7], used = 0;
  Handle    handle = 0, pid = 0, parent = 0;
  u64       image = 0;
  u32       i;
  isize     status = 0, seconds = 0;
  if (!table)
    panic("user trap without current process");
  hal_trap_decode(context, &trap);
  if (!trap.fromuser)
    panic("native trap source");
  if (trap.kind != HT_SYSCALL) {
    fault.pc = context->tf.pc;
    fault.address = trap.address;
    fault.cause = trap.code;
    procexit(LIFE_FAULT, trap.kind == HT_PAGEFAULT ? 11 : 4, &fault);
  }
  for (i = 0; i < 7; i++)
    outputs[i] = 0;
  for (i = 0; i < HAL_SYSCALL_ARGS; i++)
    args[i] = hal_syscall_arg(context, i);
  if (!known(trap.syscall_nr)) {
    status = -ERR_NOSYS;
    goto done;
  }
  switch (trap.syscall_nr) {
  case SYS_CLOCK_GETTIME:
  case SYS_CLOCK_GETRES:
  case SYS_EXIT:
  case SYS_FREE:
  case SYS_SEAL:
    used = 1;
    break;
  case SYS_ALLOC:
  case SYS_MAP:
  case SYS_UNMAP:
    used = 3;
    break;
  case SYS_YIELD:
  case SYS_GETID:
    used = 0;
    break;
  default:
    status = -ERR_NOTSUP;
    goto done;
  }
  for (i = 0; i < HAL_SYSCALL_ARGS; i++)
    if (!(used & (1UL << i)) && args[i]) {
      status = -ERR_INVALID;
      goto done;
    }
  switch (trap.syscall_nr) {
  case SYS_CLOCK_GETTIME:
  case SYS_CLOCK_GETRES:
    if (trap.syscall_nr == SYS_CLOCK_GETTIME)
      status = clocktime(args[0], &seconds, &outputs[1]);
    else
      status = clockres(args[0], &seconds, &outputs[1]);
    if (!status)
      outputs[0] = (u64) seconds;
    break;
  case SYS_EXIT:
    procexit(LIFE_EXIT, (u32) (args[0] & 255), NULL);
  case SYS_YIELD:
    procyield();
    break;
  case SYS_GETID:
    status = procidentity(&pid, &image, &parent);
    outputs[0] = (u64) pid;
    outputs[1] = image;
    outputs[2] = (u64) parent;
    break;
  case SYS_ALLOC:
    status = localnative(table, (Handle) args[0]);
    if (status)
      break;
    if (spaceread(table->space, args[1], &snapshot.alloc, sizeof(snapshot.alloc))) {
      status = -ERR_FAULT;
      break;
    }
    status = localalloc(table, (Handle) args[0], &snapshot.alloc, &handle);
    if (!status)
      outputs[0] = (u64) handle;
    break;
  case SYS_FREE:
    status = localnative(table, (Handle) args[0]);
    if (!status)
      status = localfree(table, (Handle) args[0]);
    break;
  case SYS_MAP:
    status = localnative(table, (Handle) args[0]);
    if (status)
      break;
    if (spaceread(table->space, args[1], &snapshot.map, sizeof(snapshot.map))) {
      status = -ERR_FAULT;
      break;
    }
    if (snapshot.map.reserved) {
      status = -ERR_INVALID;
      break;
    }
    status = localnative(table, snapshot.map.mem);
    if (status)
      break;
    procpausevm();
    status = localmap(table, (Handle) args[0], snapshot.map.mem, snapshot.map.offset,
                      snapshot.map.address, snapshot.map.length, snapshot.map.prot);
    procresumevm();
    break;
  case SYS_UNMAP:
    status = localnative(table, (Handle) args[0]);
    if (status)
      break;
    procpausevm();
    status = localunmap(table, (Handle) args[0], args[1]);
    procresumevm();
    break;
  case SYS_SEAL:
    status = localnative(table, (Handle) args[0]);
    if (!status)
      status = localseal(table, (Handle) args[0]);
    break;
  default:
    panic("native dispatch mismatch");
  }
done:
  hal_syscall_finish(context, status, outputs);
  hal_user_enter(context);
}
