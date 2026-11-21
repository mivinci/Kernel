#ifndef _LIBSYS_FIX_H
#define _LIBSYS_FIX_H
#include <abi/boot.h>
typedef struct Fixview  Fixview;
typedef struct Fixlayer Fixlayer;
struct Forkret;
#define FIX_LAYER_COUNT     3
#define FIX_LAYER_EMPTY     0
#define FIX_LAYER_UNSENT    1
#define FIX_LAYER_COMPLETE  2
#define FIX_LAYER_INTERRUPT 3
struct Fixlayer
{
  u64 sequence, action;
};
STATIC_ASSERT(sizeof(Fixlayer) == 16, fix_layer_size);
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
isize sysfixlayers(const Fixview *view, const Fixlayer **out);
#endif
