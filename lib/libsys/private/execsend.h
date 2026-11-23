#ifndef _LIBSYS_EXECSEND_H
#define _LIBSYS_EXECSEND_H
#include "execfd.h"
#include "operation.h"
#include "context.h"
#include "fork.h"

typedef struct Execsend Execsend;
typedef struct Forksend Forksend;
struct Execsend
{
  Opkey       key;
  Fileinherit rows[BOOT_FD_COUNT];
  isize       business;
  u64         snapshot;
};
struct Forksend
{
  Execsend send;
  Fixconn  peers[FIX_CONN_COUNT];
  u64      representative, snapshot, pin;
  Connpeer child;
  u32      cursor, count, ready;
};

/* Prepare the slot for the exact connection, using an independently owned
   File pin on it. Caller excludes caught signals until staging completes.
   Success moves the pin into the Op; failure before submission leaves it
   with the caller. Submit and reap with the ordinary Rctx/Op path, holding
   capture until the backend preparation/compensation has been resolved. */
isize sysexecfdbegin(const Execfds *capture, Connwork *work, Conncache *cache, u32 context,
                     u64 owner, u64 target_image, u64 deadline, u64 *owned_file, Execsend *send);
/* Same pin ownership rules. Zero token stages SNAPSHOT; a returned token
   stages PREPARE for the same child. */
isize sysforkfdbegin(const Execfds *capture, Connwork *work, Conncache *cache, u32 context,
                     u64 owner, Connpeer child, u64 token, u64 deadline, u64 *owned_file,
                     Execsend *send);
/* Reap an already submitted fork manifest, saving its reply before ACK.
   On success owned_file receives the operation pin and the key is cleared. */
isize sysforkfdcollect(Connwork *work, Conncache *cache, Execsend *send, u64 *owned_file);
/* Drive the original request through the runtime context. On error, the
   operation retains ownership unless collection has already released it. */
isize sysforkfddrive(Rbank *bank, u32 layer, Connwork *work, Conncache *cache, Execsend *send,
                     u64 *owned_file);
/* Select the next backend in first-fd order. Advance cursor only after
   selecting a peer. Returns 1 with a representative File, or 0 at end. */
isize sysforkfdnext(const Execfds *capture, u32 *cursor, Connpeer *peer, u64 *file);
/* Zero-initialized transaction, one stable capture and child throughout.
   Only zero return permits publication. On failure keep the transaction
   until the original request and child preparation have been resolved. */
isize sysforkfdprepare(Forksend *tx, const Execfds *capture, Connwork *works, Conncache *cache,
                       Rbank *bank, u32 layer, Connpeer child, u64 deadline);
/* Revoke publication before releasing local state. An outstanding request
   retains its slot and pin until drained; WOULD_BLOCK permits retry. */
isize sysforkfdabort(Forksend *tx, Execfds *capture, Connwork *works, Conncache *cache,
                     Elfwork *child);
/* Publish only the child for which this transaction collected every peer. */
isize sysforkfdpublish(Forksend *tx, Elfwork *child, Rbank *bank, Connpeer parent,
                       const Forkfix *fix);
/* Caller provides persistent, initially empty workspaces. If cleanup cannot
   finish, they remain owned by the parent for sysforkfdabort retry. */
isize sysforkwithfds(Handle authority, u64 reserve, const Forkresume *resume, Elfwork *work,
                      const Forkfix *fix, Execfds *capture, Forksend *tx, Connwork *works,
                      u32 layer, u64 deadline);
#endif
