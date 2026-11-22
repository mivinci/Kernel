#include "../../sbin/fs/mount.h"
#include <errno.h>
#include <string.h>

typedef struct Walktest Walktest;
typedef struct Edge     Edge;
struct Walktest
{
  Vnode   nodes[9];
  u32     refs[9], pending, reads, parents, lookups, deny;
  Vmounts mounts;
  isize   error;
};
struct Edge
{
  u32         from, to;
  const char *name;
};

static isize
hold(void *data, Vnode *node)
{
  Walktest *test = data;
  isize     error = vmounthold(&test->mounts, node->mount);
  if (error)
    return error;
  test->refs[node - test->nodes]++;
  return 0;
}

static void
drop(void *data, Vnode *node)
{
  Walktest *test = data;
  if (vmountdrop(&test->mounts, node->mount))
    test->error = -EIO;
  test->refs[node - test->nodes]--;
}

static isize
search(void *data, const Vnode *node)
{
  Walktest *test = data;
  return node - test->nodes == test->deny ? -EACCES : 0;
}

static isize
lookup(void *data, const Vnode *dir, const char *name, u32 bytes, Vnode **out)
{
  static const Edge edges[] = {{0, 1, "a"},   {0, 8, "mnt"}, {0, 7, "outside"}, {1, 4, "link"},
                               {1, 5, "rel"}, {2, 3, "sub"}, {2, 6, "file"}};
  Walktest         *test = data;
  u32               i;
  test->lookups++;
  for (i = 0; i < ARRAY_SIZE(edges); i++) {
    const Edge *edge = &edges[i];
    if (dir != &test->nodes[edge->from] || bytes != strlen(edge->name) ||
        memcmp(name, edge->name, bytes))
      continue;
    *out = &test->nodes[edge->to];
    return hold(data, *out);
  }
  return -ENOENT;
}

static isize
parent(void *data, const Vnode *dir, Vnode **out)
{
  Walktest *test = data;
  test->parents++;
  if (dir != &test->nodes[1] && dir != &test->nodes[8] && dir != &test->nodes[3])
    return -EIO;
  *out = &test->nodes[dir == &test->nodes[3] ? 2 : 0];
  return hold(data, *out);
}

static isize
readlinkstep(void *data, const Vnode *node, const char **out, u64 *bytes)
{
  Walktest *test = data;
  test->reads++;
  if (node != &test->nodes[4] && node != &test->nodes[5])
    return -EIO;
  if (test->pending) {
    test->pending = 0;
    return VWALK_WAIT;
  }
  *out = node == &test->nodes[4] ? "/mnt/sub" : "../mnt";
  *bytes = strlen(*out) + 1;
  return 0;
}

static const Vwalkops ops = {hold, drop, search, lookup, parent, readlinkstep};

static isize
runwalk(Vwalk *walk, Vnode **out)
{
  u32 i;
  for (i = 0; i < 100; i++) {
    isize status = vwalkstep(walk, out);
    if (status != VWALK_MORE)
      return status;
  }
  return -ELOOP;
}

static int
unheld(const Walktest *test)
{
  u32 i;
  for (i = 0; i < ARRAY_SIZE(test->refs); i++)
    if (test->refs[i])
      return 0;
  return !test->error;
}

int
vwalkusercheck(void)
{
  Walktest test = {{{0, 1, 1, VNODE_DIR},
                    {1, 2, 1, VNODE_DIR},
                    {0, 1, 1, VNODE_DIR},
                    {2, 2, 1, VNODE_DIR},
                    {1, 3, 1, VNODE_LINK},
                    {1, 4, 1, VNODE_LINK},
                    {2, 3, 1, VNODE_FILE},
                    {1, 5, 1, VNODE_FILE},
                    {1, 6, 1, VNODE_DIR}},
                   {0},
                   1,
                   0,
                   0,
                   0,
                   UINT32_MAX,
                   {0},
                   0};
  Vwalk    walk = {0};
  Vmounts *mounts = &test.mounts;
  Vnode   *root = &test.nodes[0], *start = &test.nodes[1], *out = NULL;
  Vnode   *child = &test.nodes[2], *point = &test.nodes[8];
  Vnode    nested = {0, 1, 1, VNODE_DIR}, replacement = {0, 1, 1, VNODE_DIR};
  if (vmountadd(mounts, root, NULL) || vmountadd(mounts, child, point) ||
      vwalkinit(&walk, mounts, &ops, &test, root, NULL, "/a/link/../file",
                sizeof("/a/link/../file"), 1) ||
      runwalk(&walk, &out) != VWALK_WAIT || out || test.lookups != 2 || test.reads != 1 ||
      test.refs[0] != 1 || test.refs[1] != 1 || test.refs[4] != 1)
    return 1;
  if (runwalk(&walk, &out) || out != &test.nodes[6] || test.reads != 2 || test.parents != 1 ||
      vmountdel(mounts, child->mount) != -EBUSY)
    return 2;
  drop(&test, out);
  out = NULL;
  if (!unheld(&test) ||
      vwalkinit(&walk, mounts, &ops, &test, root, start, "rel/file", sizeof("rel/file"), 1) ||
      runwalk(&walk, &out) || out != &test.nodes[6] || test.parents != 2)
    return 3;
  drop(&test, out);
  out = NULL;
  if (!unheld(&test) ||
      vwalkinit(&walk, mounts, &ops, &test, root, NULL, "/../mnt/../outside",
                sizeof("/../mnt/../outside"), 1) ||
      runwalk(&walk, &out) || out != &test.nodes[7] || test.parents != 3)
    return 4;
  drop(&test, out);
  out = NULL;
  if (!unheld(&test) ||
      vwalkinit(&walk, mounts, &ops, &test, root, start, "link", sizeof("link"), 0) ||
      runwalk(&walk, &out) || out != &test.nodes[4] || test.reads != 3)
    return 5;
  drop(&test, out);
  out = NULL;
  test.deny = 1;
  if (!unheld(&test) ||
      vwalkinit(&walk, mounts, &ops, &test, root, start, "./rel", sizeof("./rel"), 1) ||
      runwalk(&walk, &out) != -EACCES || out || !unheld(&test) || test.reads != 3)
    return 6;
  test.deny = UINT32_MAX;
  if (vwalkinit(&walk, mounts, &ops, &test, root, start, "/mnt/file/", sizeof("/mnt/file/"), 1) ||
      runwalk(&walk, &out) != -ENOTDIR || out || !unheld(&test))
    return 7;
  test.pending = 1;
  if (vwalkinit(&walk, mounts, &ops, &test, root, start, "link", sizeof("link"), 1) ||
      runwalk(&walk, &out) != VWALK_WAIT || out)
    return 8;
  /* The test backend has no external I/O to drain after cancellation. */
  vwalkdrop(&walk);
  if (!unheld(&test) ||
      vwalkinit(&walk, mounts, &ops, &test, point, child, "..", sizeof(".."), 1) ||
      runwalk(&walk, &out) || out != point || test.parents != 3)
    return 9;
  drop(&test, out);
  out = NULL;
  if (!unheld(&test) || vmountadd(mounts, &nested, child) ||
      vmountenter(mounts, point) != &nested || vmountparent(mounts, &nested, child) != child ||
      vmountdel(mounts, child->mount) != -EBUSY || vmountdel(mounts, nested.mount) ||
      vmountdel(mounts, child->mount) || vmounthold(mounts, child->mount) != -ESTALE ||
      vwalkinit(&walk, mounts, &ops, &test, root, NULL, "/mnt", sizeof("/mnt"), 1) ||
      runwalk(&walk, &out) || out != point)
    return 10;
  drop(&test, out);
  if (!unheld(&test) || vmountadd(mounts, &replacement, point) ||
      replacement.mount <= nested.mount || vmountenter(mounts, point) != &replacement ||
      vmountdel(mounts, replacement.mount))
    return 11;
  return 0;
}
