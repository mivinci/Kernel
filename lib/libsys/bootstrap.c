#include "private/bootstrap.h"
#include <proto/file.h>
static int
claimok(Connclaim claim)
{
  return claim.tag.generation && claim.tag.index < SYSCONN_COUNT && !claim.tag.reserved &&
         claim.seq && claim.owner;
}
static int
terminal(isize status)
{
  return !status || status == -ERR_CANCELLED || status == -ERR_PEER || status == -ERR_REVOKED;
}
static int
output(const Bootstrap *record, const void *out, u64 bytes)
{
  u64 base = (u64) (uintptr_t) record, address = (u64) (uintptr_t) out;
  return record && out && sizeof(*record) <= UINT64_MAX - base && bytes <= UINT64_MAX - address &&
         (base + sizeof(*record) <= address || address + bytes <= base);
}
static isize
match(Bootstrap *record, Connclaim claim)
{
  if (!record || !claimok(claim))
    return -ERR_INVALID;
  if (sysbootcheck(record))
    return -ERR_STATE;
  if (record->state == SYSBOOT_FREE || record->claim.tag.generation != claim.tag.generation ||
      record->claim.tag.index != claim.tag.index || record->claim.seq != claim.seq ||
      record->claim.owner != claim.owner)
    return -ERR_STALE;
  return 0;
}
void
sysbootinit(Bootstrap *record)
{
  u32 i;
  u8 *bytes = (u8 *) record;
  for (i = 0; i < sizeof(*record); i++)
    bytes[i] = 0;
}
isize
sysbootcheck(const Bootstrap *record)
{
  const u8 *bytes = (const u8 *) record;
  u32       i;
  if (!record)
    return -ERR_INVALID;
  if (record->state == SYSBOOT_FREE) {
    for (i = 0; i < sizeof(*record); i++)
      if (bytes[i])
        return -ERR_STATE;
    return 0;
  }
  if (record->state > SYSBOOT_ACKED || !claimok(record->claim) ||
      (record->opcode != FS_HELLO && record->opcode != FS_ATTACH &&
       record->opcode != FS_EXEC_CLAIM) ||
      !record->token || (record->opcode == FS_HELLO && record->token != UINT64_MAX) ||
      record->flags & ~(SYSBOOT_RESOURCE_MOVED | SYSBOOT_BAD_RESULT))
    return -ERR_STATE;
  if (record->state == SYSBOOT_PREPARED || record->state == SYSBOOT_WAITING) {
    if (record->status || record->kind || record->resource || record->flags)
      return -ERR_STATE;
    for (i = 0; i < 4; i++)
      if (record->words[i])
        return -ERR_STATE;
  } else if (!terminal(record->status))
    return -ERR_STATE;
  if (record->state == SYSBOOT_PREPARED || record->state == SYSBOOT_ACKED) {
    if (record->rpc)
      return -ERR_STATE;
  } else if (handlekind(record->rpc) != HANDLE_RPC)
    return -ERR_STATE;
  if (record->resource &&
      (handlekind(record->resource) != HANDLE_RESOURCE || record->kind != IPC_RESOURCE_REPLY ||
       record->flags & SYSBOOT_RESOURCE_MOVED))
    return -ERR_STATE;
  return 0;
}
isize
sysbootprepare(Bootstrap *record, Connclaim claim, u32 opcode, u64 token, u64 deadline)
{
  if (!record || !claimok(claim) || !token ||
      (opcode != FS_HELLO && opcode != FS_ATTACH && opcode != FS_EXEC_CLAIM) ||
      (opcode == FS_HELLO && token != UINT64_MAX))
    return -ERR_INVALID;
  if (sysbootcheck(record))
    return -ERR_STATE;
  if (record->state != SYSBOOT_FREE)
    return -ERR_BUSY;
  record->claim.tag.generation = claim.tag.generation;
  record->claim.tag.index = claim.tag.index;
  record->claim.tag.reserved = 0;
  record->claim.seq = claim.seq;
  record->claim.owner = claim.owner;
  record->opcode = opcode;
  record->token = token;
  record->deadline = deadline;
  record->state = SYSBOOT_PREPARED;
  return 0;
}
isize
sysbootsubmitted(Bootstrap *record, Connclaim claim, Handle *owned_rpc)
{
  isize error;
  if (!output(record, owned_rpc, sizeof(*owned_rpc)))
    return -ERR_INVALID;
  error = match(record, claim);
  if (error)
    return error;
  if (record->state != SYSBOOT_PREPARED)
    return -ERR_STATE;
  if (handlekind(*owned_rpc) != HANDLE_RPC)
    return -ERR_BAD_HANDLE;
  record->rpc = *owned_rpc;
  *owned_rpc = 0;
  record->state = SYSBOOT_WAITING;
  return 0;
}
isize
sysbootcapture(Bootstrap *record, Connclaim claim, isize status, const Ipcmsg *result)
{
  isize error;
  u32   i;
  error = match(record, claim);
  if (error)
    return error;
  if (record->state != SYSBOOT_WAITING)
    return -ERR_STATE;
  if (!terminal(status))
    return status;
  if (!output(record, result, sizeof(*result)))
    return -ERR_INVALID;
  record->status = status;
  record->kind = result->kind;
  for (i = 0; i < 4; i++)
    record->words[i] = result->words[i];
  if (result->kind == IPC_RESOURCE_REPLY &&
      handlekind((Handle) result->words[0]) == HANDLE_RESOURCE)
    record->resource = (Handle) result->words[0];
  if (result->source || result->image || result->request ||
      (result->kind != IPC_NONE && result->kind != IPC_RESOURCE_REPLY) ||
      (result->kind == IPC_RESOURCE_REPLY && (status || !record->resource)))
    record->flags |= SYSBOOT_BAD_RESULT;
  record->state = SYSBOOT_RESULT;
  return 0;
}
isize
sysbootpoll(Bootstrap *record, Connclaim claim)
{
  Ipcmsg result;
  isize  status, error = match(record, claim);
  if (error)
    return error;
  if (record->state == SYSBOOT_RESULT || record->state == SYSBOOT_ACKED)
    return 0;
  if (record->state != SYSBOOT_WAITING)
    return -ERR_STATE;
  status = ipc_rqpoll(record->rpc, 0, &result);
  return sysbootcapture(record, claim, status, &result);
}
isize
sysbootcancel(Bootstrap *record, Connclaim claim)
{
  isize error = match(record, claim);
  if (error)
    return error;
  if (record->state != SYSBOOT_WAITING)
    return -ERR_STATE;
  return ipc_rqcancel(record->rpc);
}
isize
sysbootack(Bootstrap *record, Connclaim claim)
{
  isize error = match(record, claim);
  if (error)
    return error;
  if (record->state != SYSBOOT_RESULT)
    return -ERR_STATE;
  error = ipc_rqack(record->rpc, ACK_RESULT | ACK_RELEASE);
  if (error)
    return error;
  record->rpc = 0;
  record->state = SYSBOOT_ACKED;
  return 0;
}
isize
sysbootresource(Bootstrap *record, Connclaim claim, Handle *out)
{
  isize error;
  if (!output(record, out, sizeof(*out)))
    return -ERR_INVALID;
  error = match(record, claim);
  if (error)
    return error;
  if ((record->state != SYSBOOT_RESULT && record->state != SYSBOOT_ACKED) || !record->resource)
    return -ERR_STATE;
  *out = record->resource;
  record->resource = 0;
  record->flags |= SYSBOOT_RESOURCE_MOVED;
  return 0;
}
isize
sysbootfinish(Bootstrap *record, Connclaim claim)
{
  isize error = match(record, claim);
  if (error)
    return error;
  if (record->state != SYSBOOT_ACKED || record->rpc || record->resource)
    return -ERR_BUSY;
  sysbootinit(record);
  return 0;
}
isize
sysbootunprepare(Bootstrap *record, Connclaim claim)
{
  isize error = match(record, claim);
  if (error)
    return error;
  if (record->state != SYSBOOT_PREPARED)
    return -ERR_STATE;
  sysbootinit(record);
  return 0;
}
