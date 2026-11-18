#ifndef _BOOTRESOURCE_H
#define _BOOTRESOURCE_H
#include <local.h>
#include <dtb.h>
/* One immutable initial-image assignment. No handles are created by query. */
int bootresourceinit(Rtable *table, Imageid image, const Dtuart *uart, Handle memory, Handle irq,
                     u32 number);
int bootresourcequery(Rtable *table, Handle system, u64 cursor, Queryrec *records, u32 count,
                      u32 *written, u64 *next);
#endif
