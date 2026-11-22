#include "file.h"
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

static void
drop(Fsfiles *table, Fsref *ref)
{
  Fsfile *file = ref->file;
  *ref = (Fsref){0};
  if (!--file->refs) {
    table->pins->drop(table->backend, file->node);
    *file = (Fsfile){0};
  }
}

isize
fsfileopen(Fsfiles *table, Openjob *job, Vnode *node, u32 access)
{
  const Ipcmsg *call = &job->call;
  Msg           reply = {{0, job->slot.seq, 0, 0}};
  Fsfile       *file = NULL;
  Fsref        *ref = NULL;
  u64           state;
  u32           i;
  isize         error, status;
  for (i = 0; i < table->nfiles; i++)
    if (!table->files[i].node) {
      file = &table->files[i];
      break;
    }
  ref = refslot(table);
  status = node->kind != VNODE_FILE                         ? -EINVAL
           : !access || (access & ~(VACCESS_R | VACCESS_W)) ? -EINVAL
           : !file || !ref                                  ? -ENFILE
           : table->serial > FS_FILE_SESSION - 3            ? -EOVERFLOW
                                                 : table->pins->hold(table->backend, node);
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
  if (status) {
    reply.words[0] = (u64) status;
    return ipc_reply(call->request, &reply);
  }
  file->session = FS_FILE_SESSION | ++table->serial;
  file->node = node;
  file->access = access;
  file->refs = 1;
  *ref = (Fsref){call->source, call->image, ++table->serial, file};
  *(volatile u64 *) (job->link->address + FS_DATA_OFFSET + call->words[3] * FS_SLOT_BYTES) =
      ref->id;
  reply.words[2] = file->session;
  reply.words[3] = sizeof(u64);
  error = ipc_reply(call->request, &reply);
  if (error)
    drop(table, ref);
  /* If the peer retired as the reply was issued, LIFE cleanup owns ref. */
  return error;
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
