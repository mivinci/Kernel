#include <syscall.h>
#include <proc.h>
#include <abi/syscall.h>
#include <diag.h>
#include <clock.h>
#include <hal/irq.h>

typedef union Arguments Arguments;
union Arguments
{
  Createpolicy policy;
  Createarg    create;
  Config       config;
  Allocarg     alloc;
  Maparg       map;
  Budgetarg    budget;
  Cred         cred;
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
  Rtable    *table = proccurrent(context);
  Htrap      trap;
  Lifefault  fault;
  Liferesult child;
  Arguments  snapshot;
  u64        args[HAL_SYSCALL_ARGS], outputs[8], used = 0;
  Handle     handle = 0, pid = 0, parent = 0;
  u64        image = 0;
  u32        i;
  isize      status = 0, seconds = 0;
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
  for (i = 0; i < 8; i++)
    outputs[i] = 0;
  for (i = 0; i < HAL_SYSCALL_ARGS; i++)
    args[i] = hal_syscall_arg(context, i);
  if (!known(trap.syscall_nr)) {
    status = -ERR_NOSYS;
    goto done;
  }
  switch (trap.syscall_nr) {
  case SYS_QUERY:
    used = 31;
    break;
  case SYS_CONFIGURE:
  case SYS_GETCRED:
    used = 7;
    break;
  case SYS_GRANT:
    used = 63;
    break;
  case SYS_WAIT:
  case SYS_CREATE:
  case SYS_SETPG:
  case SYS_INHERIT:
  case SYS_BUDGET:
  case SYS_REVOKE:
    used = 3;
    break;
  case SYS_CONNECT:
    used = 15;
    break;
  case SYS_CALL:
  case SYS_REPLY:
    used = 63;
    break;
  case SYS_SEND:
    used = 31;
    break;
  case SYS_RECV:
    used = 34;
    break;
  case SYS_REPLY_RECV:
    used = 127;
    break;
  case SYS_RQPOLL:
  case SYS_RQWAIT:
  case SYS_RQACK:
  case SYS_NOTIFY:
    used = 3;
    break;
  case SYS_DEFER:
  case SYS_RQCANCEL:
  case SYS_RQFINISH:
  case SYS_RQCHECK:
    used = 1;
    break;
  case SYS_START:
  case SYS_DESTROY:
  case SYS_GETPG:
  case SYS_TERMINATE:
  case SYS_ALARMSET:
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
  case SYS_SETSID:
  case SYS_ALARMCANCEL:
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
  if ((trap.syscall_nr >= SYS_CALL && trap.syscall_nr <= SYS_RQCHECK) ||
      trap.syscall_nr == SYS_NOTIFY || trap.syscall_nr == SYS_ALARMSET ||
      trap.syscall_nr == SYS_ALARMCANCEL) {
    status = procipc(trap.syscall_nr, args, outputs);
    hal_ipc_finish(context, status, outputs);
    proccheckpoint();
    hal_user_enter(context);
  }
  switch (trap.syscall_nr) {
  case SYS_CREATE:
    status = localcreator(table, (Handle) args[0], &snapshot.policy);
    if (status)
      break;
    if (spaceread(table->space, args[1], &snapshot.create, sizeof(snapshot.create))) {
      status = -ERR_FAULT;
      break;
    }
    status = proccreate(table, (Handle) args[0], &snapshot.create, &handle, &pid, &parent);
    if (!status) {
      outputs[0] = (u64) handle;
      outputs[1] = (u64) pid;
      outputs[2] = (u64) parent;
    }
    break;
  case SYS_CONFIGURE:
    if (spaceread(table->space, args[1], &snapshot.config, sizeof(snapshot.config))) {
      status = -ERR_FAULT;
      break;
    }
    status = args[2] > 8 ? -ERR_INVALID
                         : localconfigure(table, (Handle) args[0], &snapshot.config, (u32) args[2]);
    break;
  case SYS_START:
    status = localstart(table, (Handle) args[0]);
    break;
  case SYS_DESTROY:
    status = procdestroyview(table, (Handle) args[0]);
    break;
  case SYS_WAIT:
    if (args[1] > 7) {
      status = -ERR_INVALID;
      break;
    }
    status = procwait((Handle) args[0], (u32) args[1], &child);
    if (!status && child.pid) {
      outputs[0] = (u64) child.pid;
      outputs[1] = WAIT_EXITED;
      outputs[2] = child.code;
      if (child.reason != LIFE_EXIT)
        outputs[2] |= WAIT_SIGNALLED;
      if (child.reason == LIFE_FAULT)
        outputs[2] |= WAIT_FAULT;
      outputs[3] = child.sequence;
    }
    break;
  case SYS_GETPG:
    status = procgetpg((Handle) args[0], &handle, &pid);
    if (!status) {
      outputs[0] = (u64) handle;
      outputs[1] = (u64) pid;
    }
    break;
  case SYS_SETPG:
    status = procsetpg((Handle) args[0], (Handle) args[1]);
    break;
  case SYS_SETSID:
    status = procsetsid(&handle);
    if (!status)
      outputs[0] = (u64) handle;
    break;
  case SYS_QUERY:
    status = procquery(table, (Handle) args[0], args[1], args[2], args[3], args[4], &outputs[0],
                       &outputs[1]);
    break;
  case SYS_GETCRED:
    status = proccred(table, (Handle) args[0], (Handle) args[1], args[2], &snapshot.cred);
    if (!status) {
      outputs[0] = snapshot.cred.ruid;
      outputs[1] = snapshot.cred.euid;
      outputs[2] = snapshot.cred.rgid;
      outputs[3] = snapshot.cred.egid;
    }
    break;
  case SYS_TERMINATE:
    status = procterminate(table, (Handle) args[0]);
    break;
  case SYS_BUDGET:
    status = localnative(table, (Handle) args[0]);
    if (status)
      break;
    if (spaceread(table->space, args[1], &snapshot.budget, sizeof(snapshot.budget))) {
      status = -ERR_FAULT;
      break;
    }
    status = procbudget(table, (Handle) args[0], &snapshot.budget);
    break;
  case SYS_GRANT:
    status = procgrant(table, (Handle) args[0], (Handle) args[1], args[2], args[3], args[4],
                       args[5], &handle);
    if (!status)
      outputs[0] = (u64) handle;
    break;
  case SYS_INHERIT:
    status = localinherit(table, (Handle) args[0], args[1]);
    break;
  case SYS_REVOKE:
    status = procrevoke(table, (Handle) args[0], (Handle) args[1]);
    break;
  case SYS_CONNECT:
    status = localconnect(table, (Handle) args[0], args[1], args[2], args[3], &handle);
    if (!status)
      outputs[0] = (u64) handle;
    break;
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
    /* IRQs may mark deadlines/reschedule, but cannot drain tables or switch
       this C frame. Prepared allocations recheck the scope before publishing. */
    hal_irq_enable();
    status = localalloc(table, (Handle) args[0], &snapshot.alloc, &handle);
    (void) hal_irq_save();
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
  proccheckpoint();
  hal_user_enter(context);
}
