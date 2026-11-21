#ifndef _LIBSYS_FIX_H
#define _LIBSYS_FIX_H
#include <abi/boot.h>
typedef struct Fixview Fixview;
struct Fixview
{
  const Fix      *header;
  const Resentry *resources;
  const void     *contexts;
  u64             contexts_limit;
};
isize sysfixview(const void *data, u64 available, Handle source, u64 image, Handle target,
                 Fixview *out);
#endif
