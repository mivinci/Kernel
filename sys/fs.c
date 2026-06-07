#include <arch/riscv/spinlock.h>
#include <fs.h>
#include <kernel.h>
#include <types.h>
#include <pmm.h>

static Inode    itable[NINODE];
static SpinLock itable_lock;

void fs_init(void) {
  spin_init(&itable_lock);
  memset(itable, 0, sizeof(itable));
  printk("[fs] %d inodes, %d fds\n", NINODE, NFILE);
}

Inode *ialloc(const char *name) {
  if (!name) return NULL;

  spin_lock(&itable_lock);
  /* First, search for existing file */
  for (int i = 0; i < NINODE; i++) {
    if (itable[i].ref > 0 && strcmp(itable[i].name, name) == 0) {
      itable[i].ref++;
      spin_unlock(&itable_lock);
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
      spin_unlock(&itable_lock);
      return &itable[i];
    }
  }
  spin_unlock(&itable_lock);
  return NULL;
}

Inode *iname(const char *name) {
  if (!name) return NULL;
  spin_lock(&itable_lock);
  for (int i = 0; i < NINODE; i++) {
    if (itable[i].ref > 0 && strcmp(itable[i].name, name) == 0) {
      spin_unlock(&itable_lock);
      return &itable[i];
    }
  }
  spin_unlock(&itable_lock);
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
  spin_lock(&itable_lock);
  if (--ip->ref == 0) {
    if (ip->data) kfree(ip->data);
    memset(ip, 0, sizeof(*ip));
  }
  spin_unlock(&itable_lock);
}

/*
 */
