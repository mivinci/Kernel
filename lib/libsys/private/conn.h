#ifndef _LIBSYS_CONN_H
#define _LIBSYS_CONN_H
#include <types.h>
#include <abi/base.h>
#include "fix.h"
#define SYSCONN_COUNT      8
#define SYSCONN_FREE       0
#define SYSCONN_UNCLAIMED  1
#define SYSCONN_CONNECTING 2
#define SYSCONN_READY      3
#define SYSCONN_DRAINING   4
#define SYSCONN_RETIRED    5
typedef struct Connpeer  Connpeer;
typedef struct Conntag   Conntag;
typedef struct Connclaim Connclaim;
typedef struct Connentry Connentry;
typedef struct Conncache Conncache;
struct Connpeer
{
  Handle pid;
  u64    image;
};
struct Conntag
{
  u64 generation;
  u32 index, reserved;
};
struct Connclaim
{
  Conntag tag;
  u64     seq, owner;
};
struct Connentry
{
  u64      generation;
  Connpeer peer;
  u64      sequence, owner, connid;
  u32      state, reserved;
};
struct Conncache
{
  Connpeer  self;
  Connentry entries[SYSCONN_COUNT];
  u64       sequence;
};
STATIC_ASSERT(sizeof(Connpeer) == 16 && sizeof(Conntag) == 16, connection_identity_size);
STATIC_ASSERT(sizeof(Connclaim) == 32, connection_claim_size);
STATIC_ASSERT(sizeof(Conncache) <= PAGE_SIZE, connection_directory_page);
/* Cold native lookup of a local CALL only, never a guessed PID or Handle key.
   Caller provides valid C storage and keeps the Handle stable for the lookup. */
isize sysconnpeer(Handle call, Connpeer *out);
/* Private identity/phase directory only: no RPC/MEM/File/mapping ownership.
   Fresh unowned storage, current Image, caught signals excluded by caller.
   It may be embedded in the already budgeted connection-private work area. */
isize sysconninit(Conncache *cache, Connpeer self);
/* Read-only admission of child tokens against the inherited directory. */
isize sysconnforkcheck(Conncache *cache, Connpeer parent, const Fixconn *entries, u64 count);
/* Directory only. Caller excludes caught signals and separately repairs all
   inherited Connwork/File/Rctx state and removes private staging aliases. */
isize sysconnfork(Conncache *cache, Connpeer parent, Connpeer child, const Fixconn *entries,
                  u64 count);
isize sysconncheck(const Conncache *cache, Connpeer self);
isize sysconnbind(Conncache *cache, Connpeer peer, Conntag *out);
isize sysconnview(Conncache *cache, Conntag tag, Connentry *out);
/* owner is a nonzero private transaction identity, never a reusable index:
   an Rctx incarnation or a creator's full prospective File ID. A retained
   transport can outlive that reserve; owner stays a unique cookie and is
   never re-resolved as authority over a live File.
   begin never restarts CONNECTING; ready requires stable resources/result.
   rollback is ONLY before any request was submitted or remote effect occurred.
   These gates do not replace the required prospective RPC/resource record. */
isize sysconnbegin(Conncache *cache, Conntag tag, u64 owner, Connclaim *out);
/* Serialized ATTACH/EXEC_CLAIM on an already verified connection; preserves
   READY/connid so existing data operations can continue. */
isize sysconnreusebegin(Conncache *cache, Conntag tag, u64 owner, Connclaim *out);
/* A detached token cleanup uses the same Image-wide sequence but no entry.
   Its sentinel tag is private metadata, never a directory/authority key.
   Caller retains the exact peer CALL/token in a prepaid responsibility. */
isize sysconnabortbegin(Conncache *cache, u64 owner, Connclaim *out);
isize sysconnready(Conncache *cache, Connclaim claim, u64 connid);
isize sysconnrollback(Conncache *cache, Connclaim claim);
isize sysconninvalidate(Conncache *cache, Conntag tag);
isize sysconncleaned(Conncache *cache, Connclaim claim);
/* Only for an ended backend/ending client after all RPC, pages, File/native
   resources have been settled elsewhere. Never evict a live READY connection
   merely to get more slots or forget an unknown bootstrap result. */
isize sysconnforget(Conncache *cache, Conntag tag);
#endif
