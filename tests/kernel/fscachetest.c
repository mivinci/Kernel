#include "ktest.h"
#include "../../sbin/fs/cache.h"
#include <diag.h>
static Cacheblock blocks[2];
static u8         snapshot[CACHE_BYTES];
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("FSCACHEFAIL line=");                                                                 \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("FS block cache core");                                                                \
    }                                                                                              \
    checks++;                                                                                      \
  } while (0)
void
fscachetest(void)
{
  Cache    cache;
  Cachekey first = {0, 0, 0}, same = {0, 0, 0}, other = {0, 0, 0}, meta = {0, 0, 0},
           missing = {0, 0, 0};
  Cachekey  old;
  Cacheio   read, earlier, later, retry;
  u8       *destination, byte = 65;
  const u8 *view;
  u64       version;
  u32       i, checks = 0;
  CHECK(!cacheinit(&cache, blocks, 2, 256));
  CHECK(!cacheget(&cache, 7, CACHE_DATA, &first) &&
        cacheview(&cache, &first, &view, &version) == -ERR_WOULD_BLOCK &&
        !cachereadbegin(&cache, &first, &read, &destination));
  for (i = 0; i < CACHE_BYTES; i++)
    destination[i] = (u8) (i + 7);
  CHECK(!cacheget(&cache, 7, CACHE_DATA, &same) && same.generation == first.generation &&
        same.index == first.index &&
        cachereadbegin(&cache, &same, &later, &destination) == -ERR_BUSY &&
        !cacheput(&cache, &first) && !cacheput(&cache, &same) &&
        !cacheget(&cache, 8, CACHE_DATA, &other) &&
        cacheget(&cache, 9, CACHE_DATA, &missing) == -ERR_CAPACITY);
  CHECK(!cachecomplete(&cache, &read, 1) && !cacheget(&cache, 7, CACHE_DATA, &first) &&
        !cacheview(&cache, &first, &view, &version) && view[0] == 7 && version == 1);
  CHECK(!cacheedit(&cache, &first, 0, &byte, 1) &&
        !cachewritebegin(&cache, &first, snapshot, &earlier));
  byte = 66;
  CHECK(!cacheedit(&cache, &first, 0, &byte, 1) && snapshot[0] == 65 && view[0] == 66 &&
        !cachecomplete(&cache, &earlier, 1) && blocks[first.index].dirty &&
        !cachewritebegin(&cache, &first, snapshot, &later) &&
        cachecomplete(&cache, &earlier, 1) == -ERR_STALE && !cachecomplete(&cache, &later, 1) &&
        !blocks[first.index].dirty);
  old = first;
  CHECK(!cacheput(&cache, &first) && !cacheget(&cache, 10, CACHE_META, &meta) &&
        cacheview(&cache, &old, &view, &version) == -ERR_STALE &&
        !cachereadbegin(&cache, &meta, &read, &destination));
  for (i = 0; i < CACHE_BYTES; i++)
    destination[i] = 0;
  CHECK(!cachecomplete(&cache, &read, 1) && !cacheedit(&cache, &meta, 0, &byte, 1) &&
        cachewritebegin(&cache, &meta, snapshot, &earlier) == -ERR_STATE &&
        cacheauthorize(&cache, &meta, 1) == -ERR_STATE);
  CHECK(!cacheauthorize(&cache, &meta, 2) && !cachewritebegin(&cache, &meta, snapshot, &earlier) &&
        !cacheedit(&cache, &meta, 1, &byte, 1) && !cachecomplete(&cache, &earlier, 1) &&
        cachewritebegin(&cache, &meta, snapshot, &later) == -ERR_STATE);
  CHECK(!cacheauthorize(&cache, &meta, 3) && !cachewritebegin(&cache, &meta, snapshot, &later) &&
        !cachecomplete(&cache, &later, 1) && !cacheinvalidate(&cache, &meta));
  CHECK(!cacheput(&cache, &other) && !cacheget(&cache, 11, CACHE_DATA, &first) &&
        !cachereadbegin(&cache, &first, &read, &destination) && !cachecomplete(&cache, &read, 0) &&
        cacheview(&cache, &first, &view, &version) == -ERR_WOULD_BLOCK);
  CHECK(!cachereadbegin(&cache, &first, &retry, &destination) &&
        cachecomplete(&cache, &read, 1) == -ERR_STALE && !cachecomplete(&cache, &retry, 1));
  CHECK(cachecomplete(&cache, &retry, 1) == -ERR_STALE);
  CHECK(!cacheput(&cache, &first));
  putstr("FSCACHECHECK groups=");
  puthex(checks);
  putstr(" bytes=");
  puthex(sizeof(blocks) + sizeof(snapshot) + sizeof(cache));
  putstr("\n");
}
