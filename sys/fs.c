#include <fs.h>
#include <kernel.h>
#include <libc.h>
#include <pmm.h>

static Inode itable[NINODE];

void fs_init(void) {
  memset(itable, 0, sizeof(itable));
  printk("[fs] %d inodes, %d fds\n", NINODE, NFILE);
}

/*
 * Allocate or find an inode by name.
 */
Inode *ialloc(const char *name) {
  if (!name) return NULL;

  /* First, search for existing file */
  for (int i = 0; i < NINODE; i++) {
    if (itable[i].ref > 0 && strcmp(itable[i].name, name) == 0) {
      itable[i].ref++;
      return &itable[i];
    }
  }

  /* Create new file */
  for (int i = 0; i < NINODE; i++) {
    if (itable[i].ref == 0) {
      strcpy(itable[i].name, name);
      itable[i].data = NULL;
      itable[i].size = 0;
      itable[i].cap  = 0;
      itable[i].ref  = 1;
      return &itable[i];
    }
  }
  return NULL; /* No free inodes */
}

/*
 * Find an inode by name (without incrementing ref).
 */
Inode *iname(const char *name) {
  if (!name) return NULL;
  for (int i = 0; i < NINODE; i++) {
    if (itable[i].ref > 0 && strcmp(itable[i].name, name) == 0) return &itable[i];
  }
  return NULL;
}

/*
 * Grow an inode's data buffer to at least newsize bytes.
 */
void igrow(Inode *ip, int newsize) {
  if (newsize <= ip->cap) return;

  int   newcap  = (newsize + 4095) & ~4095; /* page-align */
  char *newdata = (char *)kalloc();
  if (!newdata) return;

  memcpy(newdata, ip->data, ip->size);
  memset(newdata + ip->size, 0, newcap - ip->size);

  if (ip->data) kfree(ip->data);

  ip->data = newdata;
  ip->cap  = newcap;
}

/*
 * Release an inode reference. Free resources if last reference.
 */
void ifree(Inode *ip) {
  if (--ip->ref == 0) {
    if (ip->data) kfree(ip->data);
    memset(ip, 0, sizeof(*ip));
  }
}
