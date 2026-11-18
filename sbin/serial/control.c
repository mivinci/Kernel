#include "control.h"
#include "private/serial.h"
#include "private/syscall.h"
#include <abi/resource.h>
#include <abi/boot.h>

static isize
peer(Serialctl *control, Handle handle, u64 image, Handle *pid)
{
  Queryrec rows[4];
  u64      cursor = 0, written, next, i, j;
  u32      batch;
  isize    error;
  if (handlekind(handle) != HANDLE_RESOURCE || !image)
    return -ERR_INVALID;
  for (batch = 0; batch < BOOT_RESOURCE_COUNT; batch++) {
    error = __sys_query(0, QUERY_RESOURCE, cursor, rows, ARRAY_SIZE(rows), &written, &next);
    if (error)
      return error;
    if (written > ARRAY_SIZE(rows) || (next && (!written || next == cursor)))
      return -ERR_STATE;
    for (i = 0; i < written; i++) {
      if (rows[i].type != QUERY_RESOURCE || rows[i].pid != control->pid ||
          rows[i].image != control->image)
        return -ERR_STATE;
      if (rows[i].data[0] != (u64) handle)
        continue;
      if (rows[i].stamp != (((u64) handle >> 16) & HANDLE_GENERATION_MAX) ||
          rows[i].data[1] != RESOURCE_NOTIFY || rows[i].data[2] != RESOURCE_USE ||
          rows[i].data[3] || rows[i].data[4] || rows[i].data[5] ||
          handlekind((Handle) rows[i].data[6]) != HANDLE_PROCESS ||
          (Handle) rows[i].data[6] == control->pid)
        return -ERR_DENIED;
      if (rows[i].data[7] != image)
        return -ERR_STALE;
      for (j = 8; j < ARRAY_SIZE(rows[i].data); j++)
        if (rows[i].data[j])
          return -ERR_STATE;
      *pid = (Handle) rows[i].data[6];
      return 0;
    }
    if (!next)
      return -ERR_BAD_HANDLE;
    cursor = next;
  }
  return -ERR_LIMIT;
}

isize
serialctlinit(Serialctl *control, Handle pid, u64 image, u64 initimage)
{
  u32 i;
  if (!control || pid == 1 || handlekind(pid) != HANDLE_PROCESS || !image || !initimage)
    return -ERR_INVALID;
  for (i = 0; i < sizeof(*control); i++)
    ((u8 *) control)[i] = 0;
  control->pid = pid;
  control->image = image;
  control->initimage = initimage;
  return 0;
}

void
serialcontrol(Serialctl *control, Serialio *io, const Ipcmsg *message, u32 online, u32 cancelled,
              Msg *out)
{
  Msg    request, reply;
  Handle target = 0;
  u32    i, opcode;
  isize  error;
  for (i = 0; i < 4; i++) {
    request.words[i] = message->words[i];
    reply.words[i] = 0;
  }
  reply.words[1] = request.words[1];
  error = -ERR_DENIED;
  if (message->kind != IPC_CALL || message->source != 1 || message->image != control->initimage)
    goto done;
  opcode = (u32) request.words[0];
  error = -ERR_NOTSUP;
  if (request.words[0] >> 32 != SERIAL_VERSION ||
      (opcode != SERIAL_BIND && opcode != SERIAL_STATUS))
    goto done;
  error = -ERR_INVALID;
  if (!request.words[1])
    goto done;
  error = -ERR_STALE;
  if (request.words[1] < control->sequence)
    goto done;
  if (request.words[1] == control->sequence) {
    error = -ERR_STATE;
    for (i = 0; i < 4; i++)
      if (request.words[i] != control->request.words[i])
        goto done;
    reply = control->reply;
    goto copy;
  }
  error = -ERR_CANCELLED;
  if (cancelled)
    goto remember;
  error = -ERR_BUSY;
  if (!online)
    goto remember;
  if (opcode == SERIAL_STATUS) {
    error = -ERR_INVALID;
    if (request.words[2] || request.words[3])
      goto remember;
    reply.words[2] = io->uart->received;
    reply.words[3] = io->uart->transmitted;
    error = 0;
  } else {
    error = -ERR_BUSY;
    if (control->peer)
      goto remember;
    error = peer(control, (Handle) request.words[2], request.words[3], &target);
    if (!error)
      error = serialwrite(io->rx, io->epoch, NULL, 0);
    if (!error)
      error = serialread(io->tx, io->epoch, NULL, 0);
    if (!error && __atomic_load_n(&io->tx->flags, __ATOMIC_ACQUIRE))
      error = -ERR_STATE;
    if (!error)
      error = ipc_notify((Handle) request.words[2], SERIAL_RX_READY | SERIAL_TX_ROOM);
    if (!error) {
      control->peer = (Handle) request.words[2];
      control->peerpid = target;
      control->peerimage = request.words[3];
      reply.words[2] = (u64) target;
      reply.words[3] = control->peerimage;
    }
  }
remember:
  reply.words[0] = (u64) error;
  control->sequence = request.words[1];
  control->request = request;
  control->reply = reply;
  goto copy;
done:
  reply.words[0] = (u64) error;
copy:
  *out = reply;
}
