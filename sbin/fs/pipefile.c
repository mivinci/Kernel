#include "pipefile.h"

static int
owner(const Piperef *ref, Handle pid, u64 image, u64 session)
{
  return ref->pid == pid && ref->image == image && ref->session == session;
}

static Pipefile *
lookup(Pipetable *table, u64 session, u32 *end)
{
  u32 i, j;
  for (i = 0; i < table->nfiles; i++)
    for (j = PIPE_READ; j <= PIPE_WRITE; j++)
      if (table->files[i].stream.refs[j] && table->files[i].session[j] == session) {
        *end = j;
        return &table->files[i];
      }
  return NULL;
}

static void
install(Piperef *ref, Handle pid, u64 image, Pipekey key)
{
  ref->pid = pid;
  ref->image = image;
  ref->session = key.session;
  ref->reference = key.reference;
}

isize
pipefilecreate(Pipetable *table, Handle pid, u64 image, u32 nonblock, Pipekey keys[2])
{
  Pipefile *file = NULL;
  Piperef  *refs[2];
  u32       i, n = 0;
  if (handlekind(pid) != HANDLE_PROCESS || !image || nonblock > 1)
    return -ERR_INVALID;
  if (table->serial > UINT64_MAX - 4)
    return -ERR_OVERFLOW;
  for (i = 0; i < table->nfiles; i++)
    if (!table->files[i].stream.refs[PIPE_READ] && !table->files[i].stream.refs[PIPE_WRITE]) {
      file = &table->files[i];
      break;
    }
  for (i = 0; i < table->nrefs && n < ARRAY_SIZE(refs); i++)
    if (!table->refs[i].pid)
      refs[n++] = &table->refs[i];
  if (!file || n != ARRAY_SIZE(refs))
    return -ERR_CAPACITY;
  pipeinit(&file->stream);
  for (i = PIPE_READ; i <= PIPE_WRITE; i++) {
    file->nonblock[i] = nonblock;
    keys[i].session = file->session[i] = ++table->serial;
    keys[i].reference = ++table->serial;
    install(refs[i], pid, image, keys[i]);
  }
  return 0;
}

isize
pipefiledup(Pipetable *table, Handle pid, u64 image, Pipekey source, u64 *reference)
{
  Pipefile *file;
  Piperef  *free = NULL;
  u32       i, end, found = 0;
  isize     error;
  for (i = 0; i < table->nrefs; i++) {
    Piperef *ref = &table->refs[i];
    if (!ref->pid && !free)
      free = ref;
    if (ref->pid && owner(ref, pid, image, source.session) && ref->reference == source.reference)
      found = 1;
  }
  if (!found || !(file = lookup(table, source.session, &end)))
    return -ERR_STALE;
  if (!free)
    return -ERR_CAPACITY;
  if (table->serial == UINT64_MAX)
    return -ERR_OVERFLOW;
  error = pipehold(&file->stream, end);
  if (error)
    return error;
  source.reference = ++table->serial;
  install(free, pid, image, source);
  *reference = source.reference;
  return 0;
}

static isize
closeone(Pipetable *table, Piperef *ref)
{
  Pipefile *file;
  u32       end;
  isize     error;
  file = lookup(table, ref->session, &end);
  if (!file)
    return -ERR_STATE;
  error = pipedrop(&file->stream, end);
  if (!error)
    ref->pid = 0;
  return error;
}

isize
pipefileclose(Pipetable *table, Handle pid, u64 image, Pipekey key)
{
  u32 i;
  for (i = 0; i < table->nrefs; i++) {
    Piperef *ref = &table->refs[i];
    if (ref->pid && owner(ref, pid, image, key.session) && ref->reference == key.reference)
      return closeone(table, ref);
  }
  return -ERR_STALE;
}

isize
pipefileretire(Pipetable *table, Handle pid, u64 image)
{
  u32 i;
  for (i = 0; i < table->nrefs; i++) {
    Piperef *ref = &table->refs[i];
    if (ref->pid && ref->pid == pid && ref->image == image) {
      isize error = closeone(table, ref);
      if (error)
        return error;
    }
  }
  return 0;
}

isize
pipefilepin(Pipetable *table, Handle pid, u64 image, u64 session, u32 end, Pipefile **out)
{
  Pipefile *file;
  u32       actual, i;
  isize     error;
  for (i = 0; i < table->nrefs; i++)
    if (table->refs[i].pid && owner(&table->refs[i], pid, image, session))
      break;
  if (i == table->nrefs || !(file = lookup(table, session, &actual)))
    return -ERR_STALE;
  if (end != actual)
    return -ERR_DENIED;
  error = pipehold(&file->stream, end);
  if (!error)
    *out = file;
  return error;
}
