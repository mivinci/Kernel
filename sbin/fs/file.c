#include "file.h"
#include "private/execmanifest.h"
#include <errno.h>

static Fsref *
find(Fsfiles *table, Handle pid, u64 image, u64 session, u64 id)
{
  u32 i;
  for (i = 0; i < table->nrefs; i++) {
    Fsref *ref = &table->refs[i];
    if (ref->pid == pid && ref->image == image && ref->id == id && ref->file &&
        ref->file->session == session)
      return ref;
  }
  return NULL;
}

static Fsref *
refslot(Fsfiles *table)
{
  u32 i;
  for (i = 0; i < table->nrefs; i++)
    if (!table->refs[i].file)
      return &table->refs[i];
  return NULL;
}

Fsfile *
fsfilefind(Fsfiles *table, Handle pid, u64 image, u64 session)
{
  u32 i;
  for (i = 0; i < table->nrefs; i++) {
    Fsref *ref = &table->refs[i];
    if (ref->file && ref->pid == pid && ref->image == image && ref->file->session == session)
      return ref->file;
  }
  return NULL;
}

isize
fsfileholdref(Fsfiles *table, Handle pid, u64 image, u64 session, u64 id, Fsfile **out)
{
  Fsref *ref;
  isize  error;
  if (!table || !out || *out || !id)
    return -ERR_INVALID;
  ref = find(table, pid, image, session, id);
  if (!ref)
    return -ERR_STALE;
  error = fsfilehold(ref->file);
  if (!error)
    *out = ref->file;
  return error;
}

isize
fsfileholdsnapshot(Fsfiles *table, const Execmanifest *snapshot, Fsfile **holds, u32 capacity)
{
  u32   i;
  isize error;
  if (!table || !holds || !snapshot || !snapshot->captured || !snapshot->count ||
      snapshot->count > BOOT_FD_COUNT || snapshot->count > capacity)
    return -ERR_INVALID;
  for (i = 0; i < snapshot->count; i++)
    if (holds[i])
      return -ERR_BUSY;
  for (i = 0; i < snapshot->count; i++) {
    const Execgroup *group = &snapshot->groups[i];
    error = fsfileholdref(table, snapshot->pid, snapshot->source_image, group->session,
                          group->reference, &holds[i]);
    if (error) {
      fsfiledropsnapshot(table, holds, i);
      return error;
    }
  }
  return 0;
}

void
fsfiledropsnapshot(Fsfiles *table, Fsfile **holds, u32 count)
{
  while (count) {
    count--;
    if (holds[count]) {
      fsfiledrop(table, holds[count]);
      holds[count] = NULL;
    }
  }
}

isize
fsfilehold(Fsfile *file)
{
  if (file->refs == UINT32_MAX)
    return -ERR_OVERFLOW;
  file->refs++;
  return 0;
}

void
fsfiledrop(Fsfiles *table, Fsfile *file)
{
  if (!--file->refs) {
    table->pins->drop(table->backend, file->node);
    *file = (Fsfile){0};
  }
}

static void
drop(Fsfiles *table, Fsref *ref)
{
  Fsfile *file = ref->file;
  *ref = (Fsref){0};
  fsfiledrop(table, file);
}

isize
fsfileoptions(const Fileslot *slot)
{
  u64 flags = slot->args[0];
  u64 mutation = FS_OPEN_CREAT | FS_OPEN_EXCL;
  u64 known = mutation | FS_OPEN_TRUNC | FS_OPEN_ACCMODE | FS_OPEN_APPEND | FS_OPEN_NONBLOCK |
              FS_OPEN_CLOEXEC;
  if (slot->object || (flags & ~known) || (flags & FS_OPEN_ACCMODE) == FS_OPEN_ACCMODE)
    return -EINVAL;
  if ((flags & mutation) == FS_OPEN_EXCL)
    return -EINVAL;
  return (flags & FS_OPEN_TRUNC) && !(flags & FS_OPEN_ACCMODE) ? -EINVAL : 0;
}

static isize
fileopen(Fsfiles *table, Openjob *job, Vnode *node, u32 owner, u32 group, u32 mode, u32 created)
{
  const Ipcmsg *call = &job->call;
  Msg           reply = {{0, job->slot.seq, 0, 0}};
  Fsfile       *file = NULL;
  Fsref        *ref = job->reserved;
  u64           state;
  u32           i, access, flags;
  isize         error, status;
  if (ref)
    goto truncate;
  flags = (u32) job->slot.args[0];
  access = flags & FS_OPEN_ACCMODE;
  access = access == FS_OPEN_RDONLY   ? VACCESS_R
           : access == FS_OPEN_WRONLY ? VACCESS_W
                                      : VACCESS_R | VACCESS_W;
  status = fsfileoptions(&job->slot);
  if (!status && node->kind != VNODE_FILE)
    status = -EINVAL;
  if (!status && !created)
    status = vaccess(&job->cred, owner, group, mode, access, 0);
  if (status)
    goto respond;
  if (!created && (flags & FS_OPEN_TRUNC) && !table->truncate) {
    status = -ENOSYS;
    goto respond;
  }
  for (i = 0; i < table->nfiles; i++)
    if (!table->files[i].node) {
      file = &table->files[i];
      break;
    }
  ref = refslot(table);
  status = !file || !ref                         ? -ENFILE
           : table->serial > FS_FILE_SESSION - 3 ? -EOVERFLOW
                                                 : table->pins->hold(table->backend, node);
respond:
  error = ipc_rqcheck(call->request, &state);
  if (error || state != RQ_CHECK_LIVE) {
    if (!status)
      table->pins->drop(table->backend, node);
    if (error)
      return error;
    if (state == RQ_CHECK_DRAINING)
      return ipc_rqfinish(call->request);
    status = -EINTR;
  }
  if (status)
    goto reject;
  file->session = FS_FILE_SESSION | ++table->serial;
  file->node = node;
  file->access = access;
  file->flags = flags & (FS_OPEN_APPEND | FS_OPEN_NONBLOCK);
  file->refs = 1;
  *ref = (Fsref){0, 0, ++table->serial, file};
  if (!created && (flags & FS_OPEN_TRUNC)) {
    job->reserved = ref;
  truncate:
    error = ipc_rqcheck(call->request, &state);
    status = table->truncate(job, node, error || state != RQ_CHECK_LIVE);
    if (status == OPEN_WAIT)
      return OPEN_WAIT;
    job->reserved = NULL;
    error = ipc_rqcheck(call->request, &state);
    if (error || state == RQ_CHECK_DRAINING || status) {
      drop(table, ref);
      if (error)
        return error;
      if (state == RQ_CHECK_DRAINING)
        return ipc_rqfinish(call->request);
      goto reject;
    }
    file = ref->file;
  }
  ref->pid = call->source;
  ref->image = call->image;
  *(volatile u64 *) (job->link->address + FS_DATA_OFFSET + call->words[3] * FS_SLOT_BYTES) =
      ref->id;
  reply.words[2] = file->session;
  reply.words[3] = sizeof(u64);
  error = ipc_reply(call->request, &reply);
  if (error)
    drop(table, ref);
  /* If the peer retired as the reply was issued, LIFE cleanup owns ref. */
  return error;
reject:
  reply.words[0] = (u64) status;
  return ipc_reply(call->request, &reply);
}

isize
fsfileopen(Fsfiles *table, Openjob *job, Vnode *node, u32 owner, u32 group, u32 mode)
{
  return fileopen(table, job, node, owner, group, mode, 0);
}

isize
fsfilecreated(Fsfiles *table, Openjob *job, Vnode *node)
{
  if (!job->parent || job->reserved || !(job->slot.args[0] & FS_OPEN_CREAT))
    return -ERR_STATE;
  return fileopen(table, job, node, 0, 0, 0, 1);
}

isize
fsfileclose(Fsfiles *table, Handle pid, u64 image, u64 session, u64 id)
{
  Fsref *ref = find(table, pid, image, session, id);
  if (!ref)
    return -ERR_STALE;
  drop(table, ref);
  return 0;
}

isize
fsfiledup(Fsfiles *table, Handle pid, u64 image, u64 session, u64 id, u64 *out)
{
  Fsref *ref = find(table, pid, image, session, id), *free;
  if (!ref)
    return -ERR_STALE;
  free = refslot(table);
  if (!free)
    return -ERR_CAPACITY;
  if (ref->file->refs == UINT32_MAX || table->serial == FS_FILE_SESSION - 1)
    return -ERR_OVERFLOW;
  ref->file->refs++;
  *free = (Fsref){pid, image, ++table->serial, ref->file};
  *out = free->id;
  return 0;
}

void
fsfileretire(Fsfiles *table, Handle pid, u64 image)
{
  u32 i;
  for (i = 0; i < table->nrefs; i++)
    if (table->refs[i].file && table->refs[i].pid == pid && table->refs[i].image == image)
      drop(table, &table->refs[i]);
}
