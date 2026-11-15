/* Internal construction preparation, never a generic userspace Op. */
#ifndef _PREP_H
#define _PREP_H
#include <local.h>
#include <life.h>
#include <prepid.h>
#ifndef PREPS
#define PREPS 192
#endif
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
  u32     state, temporary, refs;
};
int prepcreate(Rtable *source, Rtable *target, Lifeid life, Prepid *out);
int prepcheck(Prepid id, Rtable *source, Prepinfo *out);
int prepstat(Prepid id, Prepinfo *out);
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
int  prepclean(Prepid id, u32 slots);
int  prepwork(u32 scans);
void prepownerclose(Rtable *source);
void prepsourceclosed(Rtable *source);
#endif
