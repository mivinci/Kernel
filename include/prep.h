/* Internal construction preparation, never a generic userspace Op. */
#ifndef _PREP_H
#define _PREP_H
#include <local.h>
#include <life.h>
#include <image.h>
#include <prepid.h>
#ifndef PREPS
#define PREPS 192
#endif
#define PREP_NS        10000000000UL
#define PREP_ACTIVE    1
#define PREP_COMMITTED 2
#define PREP_ABORTED   3
#define PREP_MAP       CONSTRUCT_MAP
#define PREP_CONFIG    CONSTRUCT_CONFIG
#define PREP_PUBLISH   CONSTRUCT_PUBLISH
#define PREP_ABORT     CONSTRUCT_ABORT
#define PREP_ALLOC     CONSTRUCT_ALLOC

typedef struct Prepinfo Prepinfo;
struct Prepinfo
{
  Rtable *target;
  Handle  pid;
  Acctid  acct;
  Spaceid space;
  u64     image;
  u32     state, temporary, refs, expired;
  u64     deadline;
  Imageid identity;
};
int prepcreate(Rtable *source, Rtable *target, Lifeid life, Imageid identity, Prepid *out);
int prepcheck(Prepid id, Rtable *source, Prepinfo *out);
int prepstat(Prepid id, Prepinfo *out);
/* A held REQUEST supplies owner/sourceimage. No local table borrow escapes. */
int preprelated(Handle owner, u64 sourceimage, Handle target, u64 image, Imageid *out);
int prephold(Prepid id);
int prepdrop(Prepid id);
/* Publish check is reversible; finish follows the Life publication commit. */
int  preppublishcheck(Prepid id);
void prepcomplete(Prepid id);
int  prepcancel(Prepid id);
int  preptempcheck(Prepid id, Spaceid source, Memid memory);
int  preptemphold(Prepid id);
int  preptempdrop(Prepid id);
/* Invalidate immediately; cleanup releases at most 32 source slots per call. */
int prepclean(Prepid id, u32 slots);
int prepwork(u32 scans);
/* Timer upper half: scan at most eight records; no mapping or slot teardown. */
int  prepexpire(u64 now, u32 scans);
void prepownerclose(Rtable *source);
void prepsourceclosed(Rtable *source);
#endif
