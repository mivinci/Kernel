#include "private/syscall.h"
typedef isize (*Mockquery)(Handle, u64, u64, Queryrec *, u64, u64 *, u64 *);
static Mockquery query;
static Handle    identity, ancestor;
static u64       generation;
static isize     failure;
void
serialmockid(Handle pid, u64 image, Handle parent, isize error)
{
  identity = pid;
  generation = image;
  ancestor = parent;
  failure = error;
}
void
serialmockquery(Mockquery function)
{
  query = function;
}
isize
__sys_getid(Handle *pid, u64 *image, Handle *parent)
{
  *pid = failure ? 0 : identity;
  *image = failure ? 0 : generation;
  *parent = failure ? 0 : ancestor;
  return failure;
}
isize
__sys_query(Handle authority, u64 kind, u64 cursor, Queryrec *rows, u64 count, u64 *written,
            u64 *next)
{
  return query(authority, kind, cursor, rows, count, written, next);
}
