#ifndef _CHR_H
#define _CHR_H

#include <types.h>

/*
 * Character device operations — thin abstraction over hardware UART,
 * virtio-console, etc.  Upper layers (TTY, sys_read/sys_write) use
 * chr_read / chr_write / chr_has_data instead of touching hardware
 * registers directly.
 *
 * Pattern: same role as blk.h for block devices — blk_read/blk_write
 * delegate to virtio_blk_*.  Here chr_* delegates through an ops table
 * set at boot by the platform driver (e.g. uart_chr_ops in uart.c).
 */

XDEF_STRUCT(ChrOps) {
  int  (*read)(void);          /* read one byte, -1 if no data */
  void (*write)(int c);        /* write one byte (blocks until TX ready) */
  int  (*has_data)(void);      /* non-zero if at least one byte is ready */
  void (*init)(void);          /* one-time hardware init (optional) */
};

/* Called once at boot by the platform character device driver. */
void chr_set_console(ChrOps *ops);

/* Public API — delegates to console ChrOps. */
int  chr_read(void);
void chr_write(int c);
int  chr_has_data(void);

#endif /* _CHR_H */
