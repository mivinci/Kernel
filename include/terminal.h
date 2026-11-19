/* Serialized trusted terminal relationships; native authority is separate. */
#ifndef _TERMINAL_H
#define _TERMINAL_H
#include <image.h>
#include <group.h>
#ifndef TERMINALS
#define TERMINALS 64
#endif
typedef struct Terminal Terminal;
struct Terminal
{
  u64     generation, epoch;
  Imageid owner;
  Handle  session, foreground;
  u32     refs, state;
};
STATIC_ASSERT(TERMINALS > 0 && TERMINALS <= 64, terminal_capacity);
STATIC_ASSERT(sizeof(Terminal) == 56, terminal_record_size);
/* Returns one owned reference bound to an existing exact preparing/active
   image. Public terminal IDs are identities, never native authority. */
int terminalnew(Imageid owner, Handle *out);
int terminalhold(Handle id);
int terminaldrop(Handle id);
/* Internal session/group handles, not public SID/PGID. First bind only.
   Native caller must establish session/foreground permissions separately. */
int terminalbind(Handle id, Handle session);
int terminalset(Handle id, Handle group, u64 expected);
/* Checks exact image liveness on every operation. ENDED owners immediately
   reject new operations, while refs keep old relations until final drop.
   This does not perform HUP/CONT, input flushing or service recovery. */
int terminalstat(Handle id, Terminal *out);
#endif
