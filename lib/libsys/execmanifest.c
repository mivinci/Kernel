#include "private/execmanifest.h"
#include "private/syscall.h"
#include <abi/observe.h>

static int
separate(const Execmanifest *out, const void *input, u64 bytes)
{
  u64 a = (u64) (uintptr_t) out, b = (u64) (uintptr_t) input;
  return out && input && sizeof(*out) <= UINT64_MAX - a && bytes <= UINT64_MAX - b &&
         (a + sizeof(*out) <= b || b + bytes <= a);
}

static isize
manifest(Execmanifest *out, const Filewatch *watch, const Ipcmsg *call, const Fileslot *slot,
         const void *page, Connpeer backend, u64 now, Execsource lookup, void *context, u32 opcode)
{
  u64          count, bytes, span, index, i, j, used[2] = {0, 0};
  u32          groups = 0;
  isize        error;
  Fileinherit *row;
  Execgroup   *group;
  int          fork = opcode != FS_EXEC_PREPARE;
  if (!lookup || !separate(out, watch, sizeof(*watch)) || !separate(out, call, sizeof(*call)) ||
      !separate(out, slot, sizeof(*slot)) || !pidvalid(backend.pid) ||
      !backend.image)
    return -ERR_INVALID;
  if (out->count)
    return -ERR_BUSY;
  index = call->words[3];
  if (call->kind != IPC_CALL || call->words[0] != opcode ||
      handlekind(call->request) != HANDLE_REQUEST || call->source != watch->owner.pid ||
      call->image != watch->owner.image || !watch->connid || call->words[1] != watch->connid ||
      !slot->seq || slot->seq != call->words[2])
    return -ERR_STALE;
  count = slot->args[4];
  if (!count || count > BOOT_FD_COUNT || slot->args[5] || slot->args[7] || slot->object ||
      slot->args[2] != call->image || (opcode == FS_FORK_SNAPSHOT && slot->args[6]))
    return -ERR_INVALID;
  if (fork ? !pidvalid((Handle) slot->args[0]) ||
                 slot->args[0] == (u64) call->source || !slot->args[1]
           : slot->args[6] || slot->args[0] != (u64) call->source || slot->args[1] <= call->image)
    return -ERR_INVALID;
  bytes = count * sizeof(*row);
  span = bytes > FS_SLOT_BYTES ? 2 : 1;
  if (slot->payload_bytes != bytes || slot->span != span || index > FS_CONN_SLOTS - span ||
      (span == 2 && (index & 1 || index > 4)) || !separate(out, page, span * FS_SLOT_BYTES))
    return -ERR_INVALID;
  for (i = 0; i < span; i++)
    if (watch->active.requests[index + i] != call->request)
      return -ERR_STALE;
  if (slot->args[3] <= now)
    return -ERR_TIMEDOUT;
  error = __sys_copy(out->rows, page, bytes);
  if (error)
    return error;
  for (i = 0; i < count; i++) {
    row = &out->rows[i];
    if (row->fd >= BOOT_FD_COUNT || row->flags & ~(u64) BOOT_FD_CLOEXEC || !row->session ||
        !row->reference_id)
      return -ERR_INVALID;
    if (row->backend_pid != (u64) backend.pid || row->backend_image != backend.image)
      return -ERR_STALE;
    if (used[row->fd / 64] & (UINT64_C(1) << (row->fd % 64)))
      return -ERR_INVALID;
    used[row->fd / 64] |= UINT64_C(1) << (row->fd % 64);
    for (j = 0; j < groups && (out->groups[j].session != row->session ||
                               out->groups[j].reference != row->reference_id);
         j++) {
    }
    group = &out->groups[j];
    if (j == groups) {
      error = lookup(context, call->source, call->image, row->session, row->reference_id);
      if (error)
        return error;
      group->session = row->session;
      group->reference = row->reference_id;
      group->retained = group->closed = 0;
      groups++;
    }
    if (!fork && (row->flags & BOOT_FD_CLOEXEC))
      group->closed++;
    else
      group->retained++;
  }
  out->pid = call->source;
  out->source_image = call->image;
  out->target_image = slot->args[1];
  out->request = call->request;
  out->connid = call->words[1];
  out->seq = slot->seq;
  out->slot = index;
  out->deadline = slot->args[3];
  out->child = fork ? (Handle) slot->args[0] : 0;
  out->snapshot = slot->args[6];
  out->nrows = (u32) count;
  out->captured = opcode == FS_FORK_SNAPSHOT;
  out->count = groups;
  return 0;
}

isize
sysexecmanifest(Execmanifest *out, const Filewatch *watch, const Ipcmsg *call, const Fileslot *slot,
                const void *page, Connpeer backend, u64 now, Execsource lookup, void *context)
{
  return manifest(out, watch, call, slot, page, backend, now, lookup, context, FS_EXEC_PREPARE);
}

isize
sysforkmanifest(Execmanifest *out, const Filewatch *watch, const Ipcmsg *call, const Fileslot *slot,
                const void *page, Connpeer backend, u64 now, Execsource lookup, void *context)
{
  return manifest(out, watch, call, slot, page, backend, now, lookup, context, FS_FORK_PREPARE);
}

isize
sysforksnapshot(Execmanifest *out, const Filewatch *watch, const Ipcmsg *call, const Fileslot *slot,
                const void *page, Connpeer backend, u64 now, Execsource lookup, void *context,
                u64 token)
{
  isize error;
  if (!token)
    return -ERR_INVALID;
  error = manifest(out, watch, call, slot, page, backend, now, lookup, context, FS_FORK_SNAPSHOT);
  if (!error)
    out->snapshot = token;
  return error;
}

isize
sysforksnapshotcheck(const Execmanifest *snapshot, const Execmanifest *prepare, u64 now)
{
  u32 i;
  if (!snapshot || !prepare || snapshot == prepare)
    return -ERR_INVALID;
  if (!snapshot->count || !prepare->count || !snapshot->captured || prepare->captured ||
      !snapshot->snapshot || !snapshot->child || snapshot->snapshot != prepare->snapshot ||
      snapshot->pid != prepare->pid || snapshot->source_image != prepare->source_image ||
      snapshot->connid != prepare->connid || snapshot->child != prepare->child ||
      snapshot->target_image != prepare->target_image || snapshot->request == prepare->request ||
      snapshot->seq >= prepare->seq || !snapshot->nrows || snapshot->nrows > BOOT_FD_COUNT ||
      snapshot->nrows != prepare->nrows)
    return -ERR_STALE;
  if (snapshot->deadline <= now || prepare->deadline <= now)
    return -ERR_TIMEDOUT;
  for (i = 0; i < snapshot->nrows; i++) {
    const Fileinherit *a = &snapshot->rows[i], *b = &prepare->rows[i];
    if (a->fd != b->fd || a->flags != b->flags || a->backend_pid != b->backend_pid ||
        a->backend_image != b->backend_image || a->session != b->session ||
        a->reference_id != b->reference_id)
      return -ERR_STALE;
  }
  return 0;
}

static isize
snapshotref(void *context, Handle pid, u64 image, u64 session, u64 reference)
{
  const Execmanifest *snapshot = *(const Execmanifest **) context;
  u32                 i;
  if (snapshot->pid != pid || snapshot->source_image != image)
    return -ERR_STALE;
  for (i = 0; i < snapshot->count; i++)
    if (snapshot->groups[i].session == session && snapshot->groups[i].reference == reference)
      return 0;
  return -ERR_STALE;
}

isize
sysforkprepare(Execmanifest *out, const Execmanifest *snapshot, const Filewatch *watch,
               const Ipcmsg *call, const Fileslot *slot, const void *page, Connpeer backend,
               u64 now)
{
  isize error;
  if (!separate(out, snapshot, sizeof(*snapshot)))
    return -ERR_INVALID;
  if (!snapshot->count || snapshot->count > BOOT_FD_COUNT || !snapshot->captured)
    return -ERR_STALE;
  error = sysforkmanifest(out, watch, call, slot, page, backend, now, snapshotref, &snapshot);
  if (error)
    return error;
  error = sysforksnapshotcheck(snapshot, out, now);
  if (error)
    out->count = 0;
  return error;
}

static isize
observeprepared(Fileowner *owner, Handle permit, const Ipcmsg *call, u64 relation, Handle pid,
                u64 image)
{
  u64   cookie, state, reason;
  isize error;
  if (!owner || !call || call->kind != IPC_CALL || !pidvalid(call->source) ||
      handlekind(call->request) != HANDLE_REQUEST || !call->image)
    return -ERR_INVALID;
  if (owner->cookie || owner->pid || owner->image || owner->reason)
    return -ERR_BUSY;
  error = __sys_observe(permit, call->request, relation, pid, image, &cookie, &state, &reason);
  if (error)
    return error;
  if (state == OBS_ENDED)
    return -ERR_STALE;
  if (state != OBS_PREPARING || !cookie || reason)
    return -ERR_STATE;
  owner->pid = pid;
  owner->image = image;
  owner->cookie = cookie;
  return 0;
}

isize
sysexecobserve(Fileowner *owner, Handle permit, const Ipcmsg *call, u64 target)
{
  if (!call || call->words[0] != FS_EXEC_PREPARE || target <= call->image)
    return -ERR_INVALID;
  return observeprepared(owner, permit, call, OBS_FUTURE, call->source, target);
}

isize
sysforkobserve(Fileowner *owner, Handle permit, const Ipcmsg *call, Handle child, u64 image)
{
  if (!call || call->words[0] != FS_FORK_PREPARE || !pidvalid(child) ||
      child == call->source || !image)
    return -ERR_INVALID;
  return observeprepared(owner, permit, call, OBS_CHILD, child, image);
}

static isize
ownerstate(const Fileowner *owner, u64 *state, u64 *reason)
{
  Handle pid;
  u64    image;
  isize  error = __sys_obsread(owner->cookie, state, reason, &pid, &image);
  if (error)
    return error;
  return pid == owner->pid && image == owner->image ? 0 : -ERR_STALE;
}

static isize
targetstate(const Fileowner *target, u64 *reason)
{
  u64   state;
  isize error;
  if (!target || !target->cookie || !target->image)
    return -ERR_INVALID;
  error = ownerstate(target, &state, reason);
  if (error)
    return error;
  if (state == OBS_PREPARING && !*reason)
    return SYSEXEC_PENDING;
  if (state == OBS_ENDED && (*reason == OBS_ABORT || *reason == OBS_TIMEOUT))
    return SYSEXEC_ABORTED;
  if (state == OBS_ACTIVE && !*reason)
    return SYSEXEC_ACTIVE;
  if (state == OBS_ENDED && (*reason == OBS_EXIT || *reason == OBS_EXEC))
    return SYSEXEC_ENDED;
  return -ERR_STATE;
}

isize
sysforktarget(Fileowner *child)
{
  u64   reason;
  isize state = targetstate(child, &reason);
  if (state == SYSEXEC_ABORTED || state == SYSEXEC_ENDED)
    child->reason = reason;
  return state;
}

isize
sysexecstate(Fileowner *source, Fileowner *target)
{
  u64   nextreason, old, oldreason;
  isize state, error;
  if (!source || !target || !source->cookie || source->pid != target->pid || !source->image ||
      target->image <= source->image)
    return -ERR_INVALID;
  state = targetstate(target, &nextreason);
  if (state <= SYSEXEC_PENDING)
    return state;
  if (state == SYSEXEC_ABORTED) {
    target->reason = nextreason;
    return state;
  }
  /* Read the target first: its publication precedes any valid ACTIVE sample. */
  error = ownerstate(source, &old, &oldreason);
  if (error)
    return error;
  if (old != OBS_ENDED || oldreason != OBS_EXEC)
    return -ERR_STATE;
  source->reason = oldreason;
  if (state == SYSEXEC_ENDED)
    target->reason = nextreason;
  return state;
}

static isize
discard(Execreserve *record, Execdrop drop, void *context)
{
  Exechold *entry;
  isize     error;
  while (record->count) {
    entry = &record->entries[record->count - 1];
    if (entry->hold) {
      error = drop(context, entry->hold);
      if (error)
        return error;
      entry->hold = 0;
    }
    record->count--;
  }
  record->source = record->target = 0;
  record->token = 0;
  record->activated = record->retired = 0;
  return 0;
}

isize
sysexecreserve(Execreserve *out, const Execmanifest *plan, Fileowner *source, Fileowner *target,
               Exectake take, Execdrop drop, void *context)
{
  Exechold        *entry;
  const Execgroup *group;
  isize            error, cleanup;
  u32              i;
  if (!plan || plan->child || !take || !drop || !plan->count || plan->count > BOOT_FD_COUNT ||
      !separate(plan, out, sizeof(*out)))
    return -ERR_INVALID;
  if (out->count || out->ready || out->source || out->target)
    return -ERR_BUSY;
  if (!source || !target || plan->pid != source->pid || plan->source_image != source->image ||
      plan->target_image != target->image)
    return -ERR_STALE;
  error = sysexecstate(source, target);
  if (error != SYSEXEC_PENDING)
    return error < 0 ? error : -ERR_STALE;
  out->source = source->cookie;
  out->target = target->cookie;
  out->request = plan->request;
  out->connid = plan->connid;
  out->seq = plan->seq;
  out->slot = plan->slot;
  out->deadline = plan->deadline;
  for (i = 0; i < plan->count; i++) {
    entry = &out->entries[i];
    group = &plan->groups[i];
    entry->hold = 0;
    if (group->retained) {
      error = take(context, source, target, group, &entry->hold);
      if (error || !entry->hold) {
        cleanup = discard(out, drop, context);
        return cleanup ? cleanup : error ? error : -ERR_STATE;
      }
    }
    entry->group.session = group->session;
    entry->group.reference = group->reference;
    entry->group.retained = group->retained;
    entry->group.closed = group->closed;
    out->count++;
  }
  out->ready = 1;
  return 0;
}

isize
sysexecadvance(Execreserve *record, Fileowner *source, Fileowner *target, Execactivate activate,
               Execretire retire, void *context)
{
  isize state, error;
  u64   hold;
  if (!record || !source || !target || !activate || !retire || record->count > BOOT_FD_COUNT ||
      record->activated > record->count || record->retired > record->count)
    return -ERR_INVALID;
  if (!record->ready || record->source != source->cookie || record->target != target->cookie)
    return -ERR_STALE;
  state = sysexecstate(source, target);
  if (state != SYSEXEC_ACTIVE && state != SYSEXEC_ENDED)
    return state < 0 ? state : state == SYSEXEC_PENDING ? -ERR_BUSY : -ERR_STALE;
  while (state == SYSEXEC_ACTIVE && record->activated < record->count) {
    hold = record->entries[record->activated].hold;
    if (hold) {
      error = activate(context, target, hold);
      if (error)
        return error;
    }
    record->activated++;
  }
  while (record->retired < record->count) {
    error = retire(context, source, &record->entries[record->retired].group);
    if (error)
      return error;
    record->retired++;
  }
  return 0;
}

isize
sysexecdiscard(Execreserve *record, Fileowner *source, Fileowner *target, Execdrop drop,
               void *context)
{
  isize state;
  if (!record || !drop || record->count > BOOT_FD_COUNT)
    return -ERR_INVALID;
  if (!record->source && !record->target)
    return -ERR_STALE;
  if (!source || !target || record->source != source->cookie || record->target != target->cookie)
    return -ERR_STALE;
  if (record->ready) {
    state = sysexecstate(source, target);
    if (state != SYSEXEC_ABORTED && state != SYSEXEC_ENDED)
      return state < 0 ? state : -ERR_BUSY;
    if (state == SYSEXEC_ENDED && record->retired != record->count)
      return -ERR_BUSY;
    record->ready = 0;
  }
  return discard(record, drop, context);
}

isize
sysexecreply(Execreserve *record, Filewatch *watch, Filelink *link, const Ipcmsg *call, u64 token,
             u64 now)
{
  Msg        reply;
  u64        state;
  isize      error;
  Fileowner *target;
  if (!record || !watch || !link || !call || !token)
    return -ERR_INVALID;
  if (!record->ready || record->token || !link->prepaid)
    return -ERR_STATE;
  target = &link->watch.owner;
  if (record->source != watch->owner.cookie || record->target != target->cookie ||
      record->request != call->request || record->connid != watch->connid ||
      record->connid != call->words[1] || record->seq != call->words[2] ||
      record->slot != call->words[3] || record->slot >= FS_CONN_SLOTS ||
      watch->active.requests[record->slot] != record->request || watch->owner.pid != call->source ||
      watch->owner.image != call->image)
    return -ERR_STALE;
  if (now >= record->deadline)
    return -ERR_TIMEDOUT;
  error = sysexecstate(&watch->owner, target);
  if (error != SYSEXEC_PENDING)
    return error < 0 ? error : -ERR_STALE;
  error = ipc_rqcheck(record->request, &state);
  if (error)
    return error;
  if (state != RQ_CHECK_LIVE)
    return -ERR_CANCELLED;
  reply.words[0] = reply.words[3] = 0;
  reply.words[1] = record->seq;
  reply.words[2] = token;
  error = ipc_reply(record->request, &reply);
  if (error)
    return error;
  record->token = token;
  return sysfilefinish(&watch->active, record->request);
}

isize
sysexecclaim(Execreserve *record, Fileowner *source, Filelink *link, const Ipcmsg *call)
{
  Resreply delivery = {0, MAP_READ | MAP_WRITE, 0, FS_CONN_BYTES, {0, 0, FS_CONN_BYTES}};
  Msg      reply;
  u64      state;
  isize    error;
  if (!record || !source || !link || !call || call->kind != IPC_CALL ||
      handlekind(call->request) != HANDLE_REQUEST || call->words[0] != FS_EXEC_CLAIM ||
      call->words[1] || !call->words[2] || !call->words[3])
    return -ERR_INVALID;
  if (!record->ready || record->token != call->words[3] || record->source != source->cookie ||
      record->target != link->watch.owner.cookie || call->source != link->watch.owner.pid ||
      call->image != link->watch.owner.image)
    return -ERR_STALE;
  if ((!link->ready && !link->prepaid) || !link->watch.connid || !link->memory || !link->space ||
      !link->address || !link->serverimage)
    return -ERR_STATE;
  error = sysexecstate(source, &link->watch.owner);
  if (error != SYSEXEC_ACTIVE)
    return error < 0 ? error : -ERR_STALE;
  if (record->activated != record->count || record->retired != record->count)
    return -ERR_BUSY;
  error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state != RQ_CHECK_LIVE)
    return -ERR_CANCELLED;
  if (link->ready) {
    reply.words[0] = 0;
    reply.words[1] = call->words[2];
    reply.words[2] = link->watch.connid;
    reply.words[3] = FS_CONN_BYTES;
    error = ipc_reply(call->request, &reply);
  } else {
    delivery.source = link->memory;
    delivery.business[0] = call->words[2];
    delivery.business[1] = link->watch.connid;
    error = ipc_reply_resource(call->request, &delivery);
  }
  if (error)
    return error;
  record->token = 0;
  link->prepaid = 0;
  link->ready = 1;
  return 0;
}
