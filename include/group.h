/* Bounded relationship records. These functions do not authorize native jobs. */
#ifndef _GROUP_H
#define _GROUP_H
#include <handle.h>
#ifndef GROUPS
#define GROUPS 128
#endif
typedef struct Group Group;
struct Group
{
  u64    generation;
  Handle id, session;
  u32    refs, processes, members, children, kind;
};
STATIC_ASSERT(GROUPS > 1 && GROUPS <= 128, group_capacity);
STATIC_ASSERT(sizeof(Group) <= 64, group_budget);
/* create/initial return owned leases; find returns a borrowed identity. */
int sessioncreate(Handle sid, Handle *out);
int sessionfind(Handle sid, Handle *out);
int groupcreate(Handle pgid, Handle session, Handle *out);
int groupfind(Handle pgid, Handle session, Handle *out);
/* Borrow the unique process-reserved group with this public PGID. */
int groupmatch(Handle pgid, Handle *out);
/* Boot/test root relationship, never a synthetic init process. Returns a lease. */
int groupinitial(Handle *out);
/* General leases are separate from process reservations and live membership. */
int grouphold(Handle id);
int groupdrop(Handle id);
int groupattach(Handle id);
int groupdetach(Handle id);
int groupactivate(Handle id);
int groupdeactivate(Handle id);
int groupnames(Handle id, Handle *pgid, Handle *sid);
int groupidused(Handle pgid);
int groupstat(Handle id, Group *out);
int groupcheck(void);
#endif
