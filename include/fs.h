#ifndef _FS_H
#define _FS_H

#include <libc.h>

#define NFILE    16 /* Max open files in file table */
#define NINODE   16 /* Max inodes (named files) */
#define FNAMELEN 32 /* Max filename length */

/* File types */
typedef enum {
  FD_NONE,
  FD_PIPE,  /* stdin/stdout/stderr */
  FD_INODE, /* regular file */
} FileType;

/* In-memory inode (file) */
XDEF_STRUCT(Inode) {
  char  name[FNAMELEN];
  char *data; /* file data buffer */
  int   size; /* current file size */
  int   cap;  /* capacity of data buffer */
  int   ref;  /* reference count */
};

/* Open file entry (file descriptor table) */
XDEF_STRUCT(File) {
  FileType type;
  int      ref; /* reference count */
  int      readable;
  int      writable;
  Inode   *ip;  /* NULL for pipes */
  int      off; /* read/write offset */
};

/* Per-process file table: fd → File* */
XDEF_STRUCT(FileTable) {
  File *files[NFILE];
};

void   fs_init(void);
Inode *ialloc(const char *name);
Inode *iname(const char *name);
void   igrow(Inode *ip, int newsize);
void   ifree(Inode *ip);

int   fdalloc(File *f);
void  fdclose(int fd);
File *fdget(int fd);
void  fdtable_init(void);

#endif /* _FS_H */
