#ifndef _DISKFS_H
#define _DISKFS_H

/*
 * Simple on-disk filesystem.
 *
 * Layout (2048 sectors, 1 MB):
 *   Sector 0          Superblock
 *   Sectors 1..7      Inode table (16 × 7 = 112 inodes, 32 bytes each)
 *   Sectors 8..2047   Data blocks (512 bytes each)
 *
 * Each file has up to 8 direct blocks → 4 KB max.
 */

#define DFS_MAGIC   0x4449534BUL  /* "KSID" */

#define DFS_NINODES  16
#define DFS_NDIRECT  8
#define DFS_INODE_SIZE  36 /* 2+2+16+16 */
#define DFS_NAME_LEN    16
#define DFS_SB_SECTOR    0
#define DFS_INODE_SECTOR 0   /* inodes in sector 0 after superblock */
#define DFS_DATA_SECTOR  1   /* data blocks start at sector 1 */

#define DFS_MODE_FREE     0
#define DFS_MODE_REGULAR  1

/* On-disk inode */
XDEF_STRUCT(DfsInode) {
  unsigned short mode;
  unsigned short size;
  unsigned short block[DFS_NDIRECT];
  char           name[DFS_NAME_LEN];
};

/* On-disk superblock */
XDEF_STRUCT(DfsSuperblock) {
  unsigned int magic;
  unsigned int ninodes;
  unsigned int nblocks;
};

/* In-memory (cached) inode */
XDEF_STRUCT(DfsCachedInode) {
  DfsInode raw;
  int      inum;   /* inode number */
  int      dirty;  /* needs write-back */
};

/* Public API */
void    dfs_init(void);
int     dfs_open(const char *name, int *out_size);
int     dfs_read(int inum, void *buf, int offset, int size);
int     dfs_lookup(const char *name);

#endif /* _DISKFS_H */
