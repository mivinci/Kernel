#include <fs.h>
#include <kernel.h>
#include <types.h>
#include <pmm.h>

/* Per-process file table (for now, global) */
static FileTable ftable;

void fdtable_init(void) {
  memset(&ftable, 0, sizeof(ftable));
}

/*
 * Allocate a file descriptor. Returns fd number.
 */
int fdalloc(File *f) {
  for (int i = 0; i < NFILE; i++) {
    if (ftable.files[i] == NULL) {
      ftable.files[i] = f;
      f->ref++;
      return i;
    }
  }
  return -1;
}

/*
 * Close a file descriptor.
 */
void fdclose(int fd) {
  if (fd < 0 || fd >= NFILE || ftable.files[fd] == NULL) return;

  File *f          = ftable.files[fd];
  ftable.files[fd] = NULL;

  if (--f->ref == 0) {
    if (f->ip) ifree(f->ip);
    kfree(f);
  }
}

/*
 * Get the File struct for a file descriptor.
 */
File *fdget(int fd) {
  if (fd < 0 || fd >= NFILE) return NULL;
  return ftable.files[fd];
}
