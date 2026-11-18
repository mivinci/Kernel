#ifndef _SERIAL_START_H
#define _SERIAL_START_H
#include <sys/boot.h>
#include <proto/serial.h>
enum
{
  SerialSpace,
  SerialAlloc,
  SerialInit,
  SerialMmio,
  SerialIrq,
  SerialRx,
  SerialTx,
  SerialHandles
};
typedef struct Serialboot Serialboot;
struct Serialboot
{
  Handle handle[SerialHandles];
  u64    kind, offset, bytes, mapbytes, number, shift, width;
  Handle pid;
  u64    image, initimage;
};
/* Input is an immutable successful bootcheck view, disjoint from output.
   Parse the seven native Boot arguments, not truncated C argv copies. Private
   required tags are unique, flags/reserved zero, required handles distinct. Unknown
   tags remain available to future adapters but cannot replace required ones.
   Failure leaves output unchanged; numeric metadata never creates authority. */
isize serialboot(const Bootview *view, Serialboot *out);
/* Input is a successful serialboot result. Current child must have parent PID1.
   Validate actual local query identities,
   types, least rights/windows and the exact init CALL target. Bounded scan,
   no allocation, mapping, device I/O or authority installation. Only success
   stores pid/image/initimage. RESOURCE does not expose backing Mem kind or
   identity: the trusted loader still supplies MMIO and two disjoint SHARED
   rings, and native map derives their actual attributes from that authority. */
isize serialcheck(Serialboot *boot);
#endif
