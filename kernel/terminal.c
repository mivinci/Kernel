#include <terminal.h>
#include <diag.h>
static Terminal terminals[TERMINALS];

static Terminal *
find(Handle id)
{
  Terminal *terminal;
  u32       index;
  if (handletype(id) != HANDLE_TERMINAL)
    return NULL;
  index = handleindex(id);
  if (index >= TERMINALS)
    return NULL;
  terminal = &terminals[index];
  return terminal->state && terminal->generation == handlegen(id) ? terminal : NULL;
}

static int
live(Terminal *terminal)
{
  Image owner;
  return terminal && !imagestat(terminal->owner, &owner) &&
         (owner.state == IMAGE_PREPARING || owner.state == IMAGE_ACTIVE);
}

int
terminalnew(Imageid owner, Handle *out)
{
  Image     image;
  Terminal *terminal;
  u32       i;
  int       error;
  if (!out)
    return -ERR_INVALID;
  if (imagestat(owner, &image) || (image.state != IMAGE_PREPARING && image.state != IMAGE_ACTIVE))
    return -ERR_STALE;
  for (i = 0; i < TERMINALS; i++)
    if (!terminals[i].state && terminals[i].generation < HANDLE_GENERATION_MAX)
      break;
  if (i == TERMINALS)
    return -ERR_CAPACITY;
  error = imagehold(owner, IMAGE_CLEANUP);
  if (error)
    return error;
  terminal = &terminals[i];
  terminal->generation++;
  terminal->epoch = 1;
  terminal->owner = owner;
  terminal->session = terminal->foreground = 0;
  terminal->refs = terminal->state = 1;
  *out = handlepack(HANDLE_TERMINAL, terminal->generation, i);
  return 0;
}

int
terminalhold(Handle id)
{
  Terminal *terminal = find(id);
  if (!live(terminal))
    return -ERR_STALE;
  return terminalretain(id);
}
int
terminalretain(Handle id)
{
  Terminal *terminal = find(id);
  if (!terminal)
    return -ERR_STALE;
  if (terminal->refs == ~(u32) 0)
    return -ERR_OVERFLOW;
  terminal->refs++;
  return 0;
}

int
terminaldrop(Handle id)
{
  Terminal *terminal = find(id);
  if (!terminal)
    return -ERR_STALE;
  if (--terminal->refs)
    return 0;
  if (terminal->foreground && groupdrop(terminal->foreground))
    panic("terminal foreground reference");
  if (terminal->session && groupdrop(terminal->session))
    panic("terminal session reference");
  if (imagedrop(terminal->owner, IMAGE_CLEANUP))
    panic("terminal image reference");
  terminal->session = terminal->foreground = 0;
  terminal->state = 0;
  return 0;
}

int
terminalbind(Handle id, Handle session)
{
  Terminal *terminal = find(id);
  Group     relation;
  u32       i;
  int       error;
  if (!live(terminal))
    return -ERR_STALE;
  if (terminal->session)
    return -ERR_STATE;
  if (groupstat(session, &relation) || relation.kind != HANDLE_SESSION)
    return -ERR_BAD_HANDLE;
  for (i = 0; i < TERMINALS; i++)
    if (terminals[i].state && terminals[i].session == session)
      return -ERR_BUSY;
  error = grouphold(session);
  if (error)
    return error;
  terminal->session = session;
  return 0;
}

int
terminalset(Handle id, Handle group, u64 expected)
{
  Terminal *terminal = find(id);
  Group     relation;
  Handle    previous;
  int       error;
  if (!live(terminal))
    return -ERR_STALE;
  if (!terminal->session)
    return -ERR_STATE;
  if (groupstat(group, &relation) || relation.kind != HANDLE_GROUP)
    return -ERR_BAD_HANDLE;
  if (relation.session != terminal->session)
    return -ERR_DENIED;
  if (terminal->epoch != expected)
    return -ERR_STALE;
  if (terminal->epoch == UINT64_MAX)
    return -ERR_OVERFLOW;
  error = grouphold(group);
  if (error)
    return error;
  previous = terminal->foreground;
  terminal->foreground = group;
  terminal->epoch++;
  if (previous && groupdrop(previous))
    panic("old terminal foreground reference");
  return 0;
}

int
terminalimage(Handle id, Imageid *out)
{
  Terminal *terminal = find(id);
  if (!out)
    return -ERR_INVALID;
  if (!terminal)
    return -ERR_STALE;
  out->generation = terminal->owner.generation;
  out->index = terminal->owner.index;
  out->reserved = 0;
  return 0;
}

int
terminalstat(Handle id, Terminal *out)
{
  Terminal *terminal = find(id);
  if (!out)
    return -ERR_INVALID;
  if (!live(terminal))
    return -ERR_STALE;
  out->generation = terminal->generation;
  out->epoch = terminal->epoch;
  out->owner.generation = terminal->owner.generation;
  out->owner.index = terminal->owner.index;
  out->owner.reserved = terminal->owner.reserved;
  out->session = terminal->session;
  out->foreground = terminal->foreground;
  out->refs = terminal->refs;
  out->state = terminal->state;
  return 0;
}
