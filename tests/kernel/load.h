#ifndef _TEST_LOAD_H
#define _TEST_LOAD_H
#include <proc.h>
void testload(Handle pid, const u8 *data, u64 bytes, const void *boot, u64 bootbytes);
/* Leave the loaded pages owned solely by their mappings. */
void   testloadmapped(Handle pid, const u8 *data, u64 bytes, const void *boot, u64 bootbytes);
Handle testresource(Rtable *table, u32 kind);
#endif
