#include "private/execmanifest.h"
#include "private/syscall.h"

static int
separate(const Execmanifest *out, const void *input, u64 bytes)
{
  u64 a = (u64) (uintptr_t) out, b = (u64) (uintptr_t) input;
  return out && input && sizeof(*out) <= UINT64_MAX - a && bytes <= UINT64_MAX - b &&
         (a + sizeof(*out) <= b || b + bytes <= a);
}

isize
sysexecmanifest(Execmanifest *out, const Filewatch *watch, const Ipcmsg *call, const Fileslot *slot,
                const void *page, Connpeer backend, u64 now, Execsource lookup, void *context)
{
  u64          count, bytes, span, index, i, j, used[2] = {0, 0};
  u32          groups = 0;
  isize        error;
  Fileinherit *row;
  Execgroup   *group;
  if (!lookup || !separate(out, watch, sizeof(*watch)) || !separate(out, call, sizeof(*call)) ||
      !separate(out, slot, sizeof(*slot)) || handlekind(backend.pid) != HANDLE_PROCESS ||
      !backend.image)
    return -ERR_INVALID;
  if (out->count)
    return -ERR_BUSY;
  index = call->words[3];
  if (call->kind != IPC_CALL || call->words[0] != FS_EXEC_PREPARE ||
      handlekind(call->request) != HANDLE_REQUEST || call->source != watch->owner.pid ||
      call->image != watch->owner.image || !watch->connid || call->words[1] != watch->connid ||
      !slot->seq || slot->seq != call->words[2])
    return -ERR_STALE;
  count = slot->args[4];
  if (!count || count > BOOT_FD_COUNT || slot->args[5] || slot->args[6] || slot->args[7] ||
      slot->object || slot->args[0] != (u64) call->source || slot->args[1] <= call->image ||
      slot->args[2] != call->image)
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
    for (j = 0; j < groups && out->groups[j].reference != row->reference_id; j++) {
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
    } else if (group->session != row->session)
      return -ERR_STALE;
    if (row->flags & BOOT_FD_CLOEXEC)
      group->closed++;
    else
      group->retained++;
  }
  out->count = groups;
  return 0;
}
