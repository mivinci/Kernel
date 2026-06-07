#include <arch/riscv/virtio.h>
#include <diskfs.h>
#include <fs.h>
#include <kernel.h>
#include <types.h>

/* Superblock cache */
static DfsSuperblock sb;

/* In-memory inode cache — read once at mount, never touched again */
static DfsInode inode_cache[DFS_NINODES];
static int      inode_cache_valid;

/*
 * Helper: read a full sector into buf.
 */
static int dfs_read_sector(int sector, void *buf) {
  return virtio_blk_read(sector, buf);
}

/*
 * Mount the filesystem: read superblock + full inode table into cache.
 * After this, no more disk I/O during spawn paths.
 */
void dfs_init(void) {
  if (virtio_blk_capacity() == 0) {
    printk("[diskfs] no block device\n");
    return;
  }

  char buf[SECTOR_SIZE];
  if (dfs_read_sector(DFS_SB_SECTOR, buf) != 0) {
    printk("[diskfs] superblock read failed\n");
    return;
  }

  memcpy(&sb, buf, sizeof(sb));
  if (sb.magic != DFS_MAGIC) {
    printk("[diskfs] bad magic %08x (expected %08x)\n", sb.magic, DFS_MAGIC);
    return;
  }

  printk("[diskfs] mounted: %d inodes, %d blocks\n", sb.ninodes, sb.nblocks);
}

/*
 * Look up an inode by name (from cache, no disk I/O).
 */
int dfs_lookup(const char *name) {
  if (!inode_cache_valid) return -1;

  for (int i = 0; i < DFS_NINODES; i++) {
    DfsInode *ino = &inode_cache[i];
    if (ino->mode == DFS_MODE_REGULAR && strcmp(ino->name, name) == 0)
      return i;
  }
  return -1;
}

/*
 * Open a file: look up by name, return inode number and size.
 */
int dfs_open(const char *name, int *out_size) {
  int inum = dfs_lookup(name);
  if (inum < 0) return -1;
  *out_size = inode_cache[inum].size;
  return inum;
}

/*
 * Read file data from disk for a cached inode.
 */
int dfs_read(int inum, void *dst, int offset, int size) {
  if (inum < 0 || inum >= DFS_NINODES) return -1;
  if (!inode_cache_valid) return -1;

  DfsInode *ino = &inode_cache[inum];
  if (offset + size > ino->size) size = ino->size - offset;
  if (size <= 0) return 0;

  char tmp[SECTOR_SIZE];
  char *buf = dst;
  int   copied = 0;

  while (copied < size) {
    int blk_idx = offset / SECTOR_SIZE;
    int blk_off = offset % SECTOR_SIZE;
    int blk_n   = size - copied;
    if (blk_n > SECTOR_SIZE - blk_off) blk_n = SECTOR_SIZE - blk_off;

    if (blk_idx >= DFS_NDIRECT) break;
    int sector = ino->block[blk_idx];
    if (sector == 0) break;

    if (dfs_read_sector(sector, tmp) != 0) break;
    memcpy(buf + copied, tmp + blk_off, blk_n);
    copied += blk_n;
    offset += blk_n;
  }
  return copied;
}
