#ifndef _BOOTSTART_H
#define _BOOTSTART_H
#include <bootpkg.h>
/* One-shot production startup; any failure after PID1 admission is fatal. */
NORETURN void bootstart(const Bootdir *directory);
#endif
