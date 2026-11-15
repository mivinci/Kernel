/* Trusted local-table mechanisms. Caller supplies target installation authority. */
#ifndef _LOCAL_H
#define _LOCAL_H
#include <space.h>
#include <handle.h>
#include <abi/mem.h>
#include <abi/proc.h>
#include <prepid.h>
#define LOCALS          128
#define LOCAL_SPACE     2
#define LOCAL_ALLOC     3
#define LOCAL_MEM       4
#define LOCAL_CONSTRUCT 14
#define LOCAL_LOAD      4
#define LOCAL_GRANT     (1UL << 63)
#define LOCAL_FORK      1
#define LOCAL_EXEC      2
#define LOCAL_SEAL      8
#define LOCAL_OPEN      1
#define LOCAL_CLOSING   2
#define LOCAL_DEAD      3

typedef struct Local  Local;
typedef struct Rtable Rtable;
struct Local
{
  u64 generation, objectgen, rights, offset, length;
  u32 object;
  u16 subtype;
  u8  inherit, source;
};
struct Rtable
{
  Local  *slots;
  Acctid  acct;
  Spaceid space;
  u32     count, cursor, state;
  Handle  owner;
  u64     image, epoch;
};
STATIC_ASSERT(sizeof(Local) == 48, local_budget);
/* Storage is zeroed once; generations survive table close/rebinding. Context is
   borrowed from its owner, which keeps Acct/Space alive until drain completes. */
/* Current native profile excludes untimed construction/loading authority. */
int localnative(Rtable *source, Handle handle);
int localroom(Rtable *source);
int localinit(Rtable *table, Local *storage, u32 count, Acctid acct, Spaceid space);
int localconstruct(Rtable *source, Prepid prep, u64 rights, Handle *out);
int localtarget(Rtable *source, Handle target, u64 rights, Prepid *prep, Handle *pid);
int localprepared(Rtable *source, Prepid prep, Memid memory, u64 rights, Handle *out);
int localconfigure(Rtable *source, Handle target, const Config *config, u32 reserve);
int localstart(Rtable *source, Handle target);
int localdestroy(Rtable *source, Handle target);
/* Internal cleanup of one exact scope; caller bounds the scanned slot range. */
void localclearscope(Rtable *source, Prepid prep, u32 first, u32 count);
int  localmem(Rtable *table, Memid memory, u64 rights, u64 offset, u64 length, u32 inherit,
              Handle *out);
int  localspace(Rtable *table, u64 rights, Handle *out);
int  localallocator(Rtable *table, u64 rights, Handle *out);
/* This is NOT native grant: target selection/INSTALL or CONSTRUCT checks belong
   to the Proc layer. No arbitrary target table is accepted from userspace. */
int localcopy(Rtable *source, Handle handle, Rtable *target, u64 rights, u64 offset, u64 length,
              u32 inherit, Handle *out);
int localalloc(Rtable *table, Handle authority, const Allocarg *args, Handle *out);
int localmap(Rtable *table, Handle space, Handle memory, u64 offset, u64 address, u64 length,
             u64 prot);
int localunmap(Rtable *table, Handle space, u64 address);
/* Trusted kernel source only; not a user-pointer copy interface. */
int localwrite(Rtable *table, Handle handle, u64 offset, const void *data, u64 bytes);
int localseal(Rtable *table, Handle handle);
int localfree(Rtable *table, Handle handle);
int localrevoke(Rtable *table, Handle handle);
int localclose(Rtable *table);
/* At most 32 slots per call; 1 means more work, 0 means fully drained. */
int localdrain(Rtable *table, u32 slots);
#endif
