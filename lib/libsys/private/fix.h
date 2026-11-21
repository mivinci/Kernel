#ifndef _LIBSYS_FIX_H
#define _LIBSYS_FIX_H
#include <abi/boot.h>
typedef struct Fixview Fixview;
struct Forkret;
struct Fixview
{
  const Fix      *header;
  const Resentry *resources;
  const void     *contexts;
  u64             contexts_limit;
};
isize sysfixview(const void *data, u64 available, Handle source, u64 image, Handle target,
                 Fixview *out);
/* Tuple comes from the native child return or its saved signal frame. */
isize sysfixchild(const struct Forkret *result, u64 available, Handle source, u64 source_image,
                  Fixview *out);
#endif
