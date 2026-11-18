#ifndef _INIT_SERVICE_H
#define _INIT_SERVICE_H
#include "manifest.h"
#include "package.h"
#include <sys/boot.h>
#include <sys/channel.h>
#include <abi/proc.h>
#include <proto/init.h>

#define SERVICE_STARTING INIT_STATE_STARTING
#define SERVICE_READY    INIT_STATE_READY
#define SERVICE_STOPPING INIT_STATE_STOPPING
#define SERVICE_FAILED   INIT_STATE_FAILED
#ifndef SERVICE_RING_LO
#define SERVICE_RING_LO UINT64_C(0x3000000)
#endif
#define SERVICE_STRIDE UINT64_C(0x20000)
#define SERVICE_TX_OFF UINT64_C(0x10000)

typedef struct Servicestate Servicestate;
struct Servicestate
{
  u64    number, state, stage;
  Handle pid;
  u64    image;
  Handle control, view;
  u64    deadline;
  isize  error;
  u64    exitkind, exitcode, sequence, reclaimed;
  Handle rings[2];
  u64    flags;
};
STATIC_ASSERT(sizeof(Servicestate) == 128, service_state_size);
/* Actual registry state, not a second success trace or authority source. */
extern Servicestate servicestate[INITCFG_SERVICES];
extern Queryrec     serviceaccounts[INITCFG_SERVICES];
extern Queryrec     initaccount;

isize serviceinit(const Manifest *manifest, const Package *package, const Bootview *boot,
                  u64 image);
/* One bounded management pass; no synchronous userspace CALL. next is the
   nearest absolute timer deadline, or zero. work requests another CPU pass. */
isize servicestep(u64 now, u32 *work, u64 *next);
void  serviceexit(Handle pid, u64 kind, u64 code);
isize servicemessage(const Ipcmsg *message, u64 now);
/* 0 still preparing/cleaning, 1 all required READY, 3 settled required failure.
   The caller owns the overall boot-failure/diagnostic policy. */
u32 servicephase(void);
#endif
