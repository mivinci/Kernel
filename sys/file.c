#include <spinlock.h>
#include <fs.h>
#include <kernel.h>
#include <types.h>
#include <pmm.h>

/* Per-process file table (for now, global) */
static FileTable ftable;
static SpinLock  ftable_lock;

void fdtable_init(void) {
  spin_init(&ftable_lock);
  memset(&ftable, 0, sizeof(ftable));
}

int fdalloc(File *f) {
  spin_lock(&ftable_lock);
  for (int i = 0; i < NFILE; i++) {
    if (ftable.files[i] == NULL) {
      ftable.files[i] = f;
      f->ref++;
      spin_unlock(&ftable_lock);
      return i;
    }
  }
  spin_unlock(&ftable_lock);
  return -1;
}

void fdclose(int fd) {
  if (fd < 0 || fd >= NFILE) return;
  spin_lock(&ftable_lock);
  if (ftable.files[fd] == NULL) { spin_unlock(&ftable_lock); return; }
  File *f = ftable.files[fd];
  ftable.files[fd] = NULL;
  spin_unlock(&ftable_lock);

  if (--f->ref == 0) {
    if (f->ip) ifree(f->ip);
    kfree(f);
  }
}

File *fdget(int fd) {
  if (fd < 0 || fd >= NFILE) return NULL;
  return ftable.files[fd];
}
