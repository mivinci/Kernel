#include <arch/riscv/virtio.h>
#include <diskfs.h>
#include <fs.h>
#include <kernel.h>
#include <types.h>

/* Superblock cache */
static DfsSuperblock sb;

/*
 * Helper: read a full sector into buf.
 */
static int dfs_read_sector(int sector, void *buf) {
  return virtio_blk_read(sector, buf);
}

/*
 * Mount the filesystem: read superblock, validate.
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

  printk("[diskfs] %d inodes, %d blocks\n", sb.ninodes, sb.nblocks);

  /* Backward compat: also register as ramfs idiskslot entries.
   * usr_load (old code) uses iname/Inode lookup. */
  char secbuf[SECTOR_SIZE];
  for (int sec = 0; sec < 7; sec++) {
    if (dfs_read_sector(DFS_INODE_SECTOR + sec, secbuf) != 0) continue;
    for (int i = 0; i < 16; i++) {
      DfsInode *ino = (DfsInode *)(secbuf + i * DFS_INODE_SIZE);
      if (ino->mode != DFS_MODE_REGULAR) continue;
      /* Register: name → sector */
      Inode *ip = idiskslot(ino->name, ino->block[0], ino->size);
      if (ip)
        printk("[boot] %-16s sector=%d size=%d\n", ino->name, ino->block[0], ino->size);
    }
  }
}

/*
 * Read an inode from disk.
 */
static int dfs_read_inode(int inum, DfsInode *ino) {
  if (inum < 0 || inum >= DFS_NINODES) return -1;

  static char buf[SECTOR_SIZE];
  int sec  = DFS_INODE_SECTOR + inum / 16;
  int off  = (inum % 16) * DFS_INODE_SIZE;

  if (dfs_read_sector(sec, buf) != 0) return -1;
  memcpy(ino, buf + off, DFS_INODE_SIZE);
  return 0;
}

/*
 * Look up an inode by name.  Returns inode number, or -1.
 */
int dfs_lookup(const char *name) {
  static char buf[SECTOR_SIZE];

  for (int sec = 0; sec < 7; sec++) {
    if (dfs_read_sector(DFS_INODE_SECTOR + sec, buf) != 0) continue;

    for (int i = 0; i < 16; i++) {
      DfsInode *ino = (DfsInode *)(buf + i * DFS_INODE_SIZE);
      if (ino->mode == DFS_MODE_REGULAR && strcmp(ino->name, name) == 0)
        return sec * 16 + i;
    }
  }
  return -1;
}

/*
 * Open a file: look up by name, return inode number and size.
 * Returns inode number, or -1.
 */
int dfs_open(const char *name, int *out_size) {
  int inum = dfs_lookup(name);
  if (inum < 0) return -1;

  DfsInode ino;
  if (dfs_read_inode(inum, &ino) < 0) return -1;

  *out_size = ino.size;
  return inum;
}

/*
 * Read data from a file (by inode number).
 * Returns bytes read, or -1 on error.
 */
int dfs_read(int inum, void *dst, int offset, int size) {
  if (inum < 0 || inum >= DFS_NINODES) return -1;

  DfsInode ino;
  if (dfs_read_inode(inum, &ino) < 0) return -1;

  if (offset + size > ino.size) size = ino.size - offset;
  if (size <= 0) return 0;

  char  *buf  = dst;
  int    copied = 0;

  while (copied < size) {
    int blk_idx  = offset / SECTOR_SIZE;
    int blk_off  = offset % SECTOR_SIZE;
    int blk_n    = SECTOR_SIZE - blk_off;
    if (blk_n > size - copied) blk_n = size - copied;

    if (blk_idx >= DFS_NDIRECT) break;

    int sector = ino.block[blk_idx];
    if (sector == 0) break; /* sparse block */

    char tmp[SECTOR_SIZE];
    if (dfs_read_sector(sector, tmp) != 0) break;

    memcpy(buf, tmp + blk_off, blk_n);
    buf     += blk_n;
    offset  += blk_n;
    copied  += blk_n;
  }

  return copied;
}
