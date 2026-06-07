#ifndef _BLK_H
#define _BLK_H

#include <types.h>

/* SECTOR_SIZE is the common minimum I/O unit */
#define SECTOR_SIZE 512

int           blk_read(unsigned long sector, void *buf);
int           blk_write(unsigned long sector, const void *buf);
unsigned long blk_capacity(void);

#endif /* _BLK_H */