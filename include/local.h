/* Trusted local-table mechanisms. Caller supplies target installation authority. */
#ifndef _LOCAL_H
#define _LOCAL_H
#include <space.h>
#include <handle.h>
#include <abi/mem.h>
#include <abi/proc.h>
#include <abi/ipc.h>
#include <abi/resource.h>
#include <abi/system.h>
#include <prepid.h>
#include <image.h>
#include <irq.h>
#define LOCALS           128
#define LOCAL_CREATE     RESOURCE_CREATE
#define LOCAL_SPACE      RESOURCE_SPACE
#define LOCAL_ALLOC      RESOURCE_ALLOC
#define LOCAL_MEM        RESOURCE_MEM
#define LOCAL_CALL       RESOURCE_CALL
#define LOCAL_SEND       RESOURCE_SEND
#define LOCAL_NOTIFY     RESOURCE_NOTIFY
#define LOCAL_CONTROL    RESOURCE_CONTROL
#define LOCAL_OBSERVE    RESOURCE_OBSERVE
#define LOCAL_IRQ_BIND   RESOURCE_IRQ_BIND
#define LOCAL_IRQ_SOURCE RESOURCE_IRQ_SOURCE
#define LOCAL_SYSTEM     RESOURCE_SYSTEM
#define LOCAL_USE        RESOURCE_USE
#define LOCAL_CONSTRUCT  RESOURCE_CONSTRUCT
#define LOCAL_LOAD       4
#define LOCAL_GRANT      RESOURCE_GRANT
#define LOCAL_FORK       1
#define LOCAL_EXEC       2
#define LOCAL_SEAL       8
#define LOCAL_OPEN       1
#define LOCAL_CLOSING    2
#define LOCAL_DEAD       3
/* Internal empty-slot ownership, never public RESOURCE subtypes. */
#define LOCAL_DRAFT   0xfffdU
#define LOCAL_STAGED  0x80U
#define LOCAL_FLOOR   0xfffeU
#define LOCAL_RECEIVE 0xffffU

typedef struct Createpolicy Createpolicy;
typedef struct Local        Local;
typedef struct Rtable       Rtable;
typedef struct Forkbinding  Forkbinding;
struct Createpolicy
{
  Setid set;
  u64   memlimit, exec_limit, rights;
  u32   resources, pending;
};
STATIC_ASSERT(sizeof(Createpolicy) == 48, createpolicy_size);
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
struct Forkbinding
{
  Memid source, target;
};
STATIC_ASSERT(sizeof(Forkbinding) == 32, fork_binding_size);
/* Trusted table transaction, empty target/new child identity only. Exact slot
   and generation values derive from the parent lineage; noninherited slots
   retain tombstones. Bindings are prepared child views, never user input.
   Does not clone mappings/unmapped Mem or publish a Proc. Limits describe the
   actual child Acct/table/pending specification for CREATE attenuation. */
int localfork(Rtable *source, Rtable *target, const Forkbinding *bindings, u32 count,
              const Createarg *limits);
/* Unpublished fork staging. Draft providers own one Mem manager; all acquired
   references stay in the target table, hidden from Handle/query operations.
   Old target generations remain untouched until finish, so drop/normal drain
   need no source table or caller-stack generation/ownership list. The caller
   freezes source/target and separately owns Space/Signal/Prep transactions. */
int localforkview(Rtable *target, Memid original, Memid *out);
int localforkbind(Rtable *source, Rtable *target, u32 index, Memid view);
int localforkload(Rtable *source, Rtable *target, const Createarg *limits);
/* Floor eligibility follows the future parent lineage, not the old run slot. */
int localforkreserve(Rtable *source, Rtable *target, Imageid image, u32 count);
/* Only publishes Handle lineage; backing/PTE commit belongs to the caller. */
int localforkfinish(Rtable *source, Rtable *target);
int localforkdrop(Rtable *target);
STATIC_ASSERT(sizeof(Local) == 48, local_budget);
/* Storage is zeroed once; generations survive table close/rebinding. Context is
   borrowed from its owner, which keeps Acct/Space alive until drain completes. */
/* CREATE transfer remains gated; each operation checks its own prepare scope. */
int localnative(Rtable *source, Handle handle);
int localroom(Rtable *source);
int localrooms(Rtable *source, u32 count);
/* Atomic PREPARING-image floor replacement. No visible Handle is installed. */
int localreserve(Rtable *table, Imageid image, u32 count);
/* A request holds the exact Image until release; its Proc table cannot rebind
   before caller death has returned every outstanding receive slot. */
int localreceive(Rtable *table, Imageid image, Handle rpc, u16 *slot);
int localreturn(Rtable *table, Imageid image, Handle rpc, u16 slot);
/* Typed cold path; only the request layer supplies target/slot and consent. */
int localreplycheck(Rtable *source, u32 kind, const Resreply *reply);
int localdeliver(Rtable *source, u32 kind, const Resreply *reply, Rtable *target, Imageid image,
                 Handle rpc, u16 slot, Handle *out);
/* Trusted boot policy installation; no native creation admission here. */
int localcreate(Rtable *table, const Createpolicy *policy, Handle *out);
int localcreator(Rtable *table, Handle authority, Createpolicy *out);
/* Numeric bounds only; native Proc must separately validate credentials/parent. */
int localcreatecheck(Rtable *table, Handle authority, const Createarg *args, Createpolicy *out);
int localchildcreate(Rtable *source, Handle authority, Rtable *target, const Createarg *args,
                     Handle *out);
/* Trusted bounded snapshots; caller resolves the table authority. */
int localquery(Rtable *table, u64 cursor, Queryrec *records, u32 count, u32 *written, u64 *next);
/* CONFIG preparation table borrow; only native RESOURCE query may use it. */
int localqueryprep(Rtable *source, Handle authority, Rtable **target);
int localinit(Rtable *table, Local *storage, u32 count, Acctid acct, Spaceid space);
/* Trusted installation only; native connect must supply CONFIG/CONNECT authority. */
int localendpoint(Rtable *table, Imageid target, u32 kind, u64 rights, u64 bits, u32 inherit,
                  Handle *out);
int localcontrol(Rtable *table, Lifeid life, u64 rights, Handle *out);
int localobserve(Rtable *table, u64 rights, Handle *out);
/* Trusted target installation; no raw-number native bind admission here. */
int localirqbind(Rtable *table, Imageid image, Event *event, u32 source, u32 bit, Handle *out);
int localirqsource(Rtable *table, u32 number, Handle *out);
int localirqresolve(Rtable *table, Handle handle, u32 *source);
int localsystem(Rtable *table, Imageid image, Handle *out);
int localsystemquery(Rtable *table, Handle authority, Imageid *out);
int localirqack(Rtable *table, Handle binding, Imageid image);
int localirqunbind(Rtable *table, Handle binding, Imageid image);
int localobserver(Rtable *table, Handle permit, u32 relation);
int localcontrolled(Rtable *table, Handle control, u64 rights, Lifeid *life, u64 *available);
int localconnect(Rtable *table, Handle authority, u64 kind, u64 rights, u64 bits, Handle *out);
int localchannel(Rtable *table, Handle handle, u32 kind, Imageid *target, u64 *bits);
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
int localinherit(Rtable *table, Handle handle, u64 flags);
int localfree(Rtable *table, Handle handle);
int localrevoke(Rtable *table, Handle handle);
int localclose(Rtable *table);
/* At most 32 slots per call; 1 means more work, 0 means fully drained. */
int localdrain(Rtable *table, u32 slots);
#endif
