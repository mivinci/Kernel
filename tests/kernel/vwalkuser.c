#include "../../sbin/fs/mount.h"
#include "../../sbin/fs/context.h"
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
  u64     version;
  u32     cwdwait, unlinked;
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

static u64
cwdversion(void *data)
{
  return ((Walktest *) data)->version;
}

static isize
cwdname(void *data, const Vnode *node, Vnode **out, const char **name, u32 *length)
{
  Walktest *test = data;
  if (node == &test->nodes[3]) {
    if (test->unlinked)
      return -ENOENT;
    *name = test->version ? "renamed" : "sub";
    *out = &test->nodes[2];
  } else if (node == &test->nodes[8]) {
    if (test->cwdwait) {
      test->cwdwait = 0;
      return VWALK_WAIT;
    }
    *name = test->version ? "moved" : "mnt";
    *out = &test->nodes[0];
  } else
    return -ENOENT;
  *length = strlen(*name);
  return hold(data, *out);
}

static const Fscwdops cwdops = {hold, drop, cwdversion, cwdname};

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

static int
cwdcheck(Walktest *test)
{
  Fscontext context = {0};
  Fscwd     work = {0};
  char      path[20] = "sentinel";
  u32       bytes = 99, i;
  context.root = &test->nodes[0];
  context.cwd = &test->nodes[3];
  test->cwdwait = 1;
  if (fscwdinit(&work, &test->mounts, &cwdops, test, &context) ||
      fscwdstep(&work, path, sizeof(path), &bytes) != VWALK_MORE ||
      fscwdstep(&work, path, sizeof(path), &bytes) != VWALK_WAIT ||
      vmountdel(&test->mounts, context.cwd->mount) != -EBUSY || strcmp(path, "sentinel") ||
      bytes != 99)
    return 1;
  test->version++;
  if (fscwdstep(&work, path, sizeof(path), &bytes) != VWALK_MORE ||
      fscwdstep(&work, path, sizeof(path), &bytes) != VWALK_MORE ||
      fscwdstep(&work, path, sizeof(path), &bytes) != VWALK_MORE ||
      fscwdstep(&work, path, sizeof(path), &bytes) || strcmp(path, "/moved/renamed") ||
      bytes != sizeof("/moved/renamed") || !unheld(test))
    return 2;
  context.root = &test->nodes[2];
  if (fscwdinit(&work, &test->mounts, &cwdops, test, &context) ||
      fscwdstep(&work, path, sizeof(path), &bytes) != VWALK_MORE ||
      fscwdstep(&work, path, 2, &bytes) != -ERANGE || strcmp(path, "/moved/renamed") ||
      bytes != sizeof("/moved/renamed") || !unheld(test))
    return 3;
  context.root = &test->nodes[8];
  context.cwd = &test->nodes[2];
  if (fscwdinit(&work, &test->mounts, &cwdops, test, &context) ||
      fscwdstep(&work, path, sizeof(path), &bytes) != -ENOENT || !unheld(test))
    return 7;
  context.root = &test->nodes[2];
  context.cwd = &test->nodes[3];
  test->unlinked = 1;
  if (fscwdinit(&work, &test->mounts, &cwdops, test, &context) ||
      fscwdstep(&work, path, sizeof(path), &bytes) != -ENOENT || !unheld(test))
    return 4;
  context.cwd = context.root;
  if (fscwdinit(&work, &test->mounts, &cwdops, test, &context))
    return 5;
  for (i = 0; i < 3; i++) {
    test->version++;
    if (fscwdstep(&work, path, sizeof(path), &bytes) != VWALK_MORE)
      return 5;
  }
  test->version++;
  if (fscwdstep(&work, path, sizeof(path), &bytes) != -EAGAIN || !unheld(test) ||
      fscwdinit(&work, &test->mounts, &cwdops, test, &context) ||
      fscwdstep(&work, path, sizeof(path), &bytes) || strcmp(path, "/") || bytes != 2 ||
      !unheld(test))
    return 6;
  return 0;
}

static int
contextcheck(Walktest *test)
{
  Fscontext        entries[1] = {{0}};
  Fscontexts       table = {entries, ARRAY_SIZE(entries), &ops, test, 0};
  const Fscontext *entry;
  Vnode           *root = &test->nodes[0], *cwd = &test->nodes[2], *out = NULL;
  Vnode            stale = {UINT64_MAX, 1, 1, VNODE_DIR};
  Vwalk            walk = {0};
  u64              version;
  const Handle     pid = (UINT64_C(1) << 59) | (UINT64_C(1) << 16) | 2;
  const Handle     reused = pid + (UINT64_C(1) << 16);
  if (fscontextfind(&table, pid, 1) || fscontextadd(&table, pid, 1, root, &stale, 027) != -ESTALE ||
      !unheld(test) || fscontextadd(&table, pid, 1, root, root, 027))
    return 1;
  entry = fscontextfind(&table, pid, 1);
  if (!entry || entry->root != root || entry->cwd != root || entry->mask != 027 ||
      test->refs[0] != 2 || fscontextfind(&table, pid, 2) || fscontextfind(&table, reused, 1) ||
      fscontextremove(&table, pid, 2) != -ESTALE ||
      fscontextadd(&table, pid, 1, root, root, 0) != -EEXIST ||
      fscontextadd(&table, reused, 1, root, root, 0) != -ENFILE)
    return 2;
  version = entry->version;
  if (fscontextchdir(&table, pid, 1, &stale) != -ESTALE || entry->version != version ||
      entry->cwd != root || fscontextchdir(&table, pid, 1, cwd) || entry->version <= version ||
      entry->cwd != cwd || test->refs[0] != 1 || test->refs[2] != 1 ||
      vmountdel(&test->mounts, cwd->mount) != -EBUSY)
    return 3;
  if (vwalkinit(&walk, &test->mounts, &ops, test, entry->root, entry->cwd, ".", sizeof("."), 1) ||
      runwalk(&walk, &out) || out != cwd)
    return 4;
  drop(test, out);
  out = NULL;
  /* Start an absolute traversal, then drop the context before stepping it. */
  version = entry->version;
  if (vwalkinit(&walk, &test->mounts, &ops, test, entry->root, entry->cwd, "/../", sizeof("/../"),
                1) ||
      fscontextremove(&table, pid, 1) || fscontextfind(&table, pid, 1) || runwalk(&walk, &out) ||
      out != root || test->refs[2])
    return 5;
  drop(test, out);
  if (!unheld(test) || fscontextadd(&table, reused, 1, &test->nodes[0], &test->nodes[0], 077) ||
      fscontextfind(&table, pid, 1) || !(entry = fscontextfind(&table, reused, 1)) ||
      entry->version <= version || entry->mask != 077 || fscontextremove(&table, reused, 1) ||
      !unheld(test))
    return 6;
  return 0;
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
                   0,
                   0,
                   0,
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
  if (contextcheck(&test))
    return 12;
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
  if (cwdcheck(&test))
    return 13;
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
