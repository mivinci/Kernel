#ifndef _BOOTSTART_H
#define _BOOTSTART_H
#include <bootpkg.h>
#include <dtb.h>
/* One-shot production startup; any failure after PID1 admission is fatal. */
NORETURN void bootstart(const Bootdir *directory, const Dtinfo *hardware, const Dtuart *uart);
#endif
