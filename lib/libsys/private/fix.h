#ifndef _LIBSYS_FIX_H
#define _LIBSYS_FIX_H
#include <abi/boot.h>
typedef struct Fixview  Fixview;
typedef struct Fixlayer Fixlayer;
typedef struct Fixconn  Fixconn;
typedef struct Fixspec  Fixspec;
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
#define FIX_CONN_COUNT 8
struct Fixconn
{
  Handle pid;
  u64    image, token;
};
STATIC_ASSERT(sizeof(Fixconn) == 24, fix_connection_size);
#define FIX_BASE_BYTES (sizeof(Fix) + FIX_LAYER_COUNT * sizeof(Fixlayer) + sizeof(u64))
struct Fixspec
{
  Handle          source, target;
  u64             image;
  const Fixlayer *layers;
  const Fixconn  *connections;
  u64             connection_count;
  const Resentry *resources;
  u64             resource_count;
};
/* Returns the encoded length or a negative status; errors leave data unchanged.
   All input tables must be stable until the pack completes. */
isize sysfixpack(const Fixspec *spec, void *data, u64 capacity);
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
isize sysfixconnections(const Fixview *view, const Fixconn **out, u64 *count);
isize sysfixtoken(const Fixview *view, Handle pid, u64 image, u64 *token);
#endif
