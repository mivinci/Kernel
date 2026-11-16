/* Fixed-image notification responsibility; never proof of business/DMA cleanup. */
#ifndef _OBSERVE_H
#define _OBSERVE_H
#include <image.h>
#include <abi/observe.h>
#ifndef OBSERVATIONS
#define OBSERVATIONS 1024
#endif
#ifndef OBS_GENERATION_MAX
#define OBS_GENERATION_MAX (((u64) 1 << 48) - 1)
#endif
STATIC_ASSERT(OBS_GENERATION_MAX > 0 && OBS_GENERATION_MAX <= (((u64) 1 << 48) - 1),
              observation_generation_limit);
#define OBS_QUOTA 128
#define OBS_BANKS (OBSERVATIONS / OBS_QUOTA)
typedef struct Obsresult Obsresult;
struct Obsresult
{
  u64    cookie;
  Handle pid;
  u64    image;
  u32    state, reason;
};
STATIC_ASSERT(OBSERVATIONS >= OBS_QUOTA && OBSERVATIONS <= 65536 && OBSERVATIONS % OBS_QUOTA == 0,
              observation_capacity);
/* One reservation shared by all permits of this exact observer image. */
int obsreserve(Imageid owner);
int obsrelease(Imageid owner);
int obsregister(Imageid owner, Imageid target, Obsresult *out);
int obsread(Imageid owner, u64 cookie, Obsresult *out);
int obsnext(Imageid owner, u64 cursor, Obsresult *out, u64 *next);
int obsdrop(Imageid owner, u64 cookie, u32 ended);
/* Called before detaching an exiting/replaced observer; <=32 records per step. */
int obsclose(Imageid owner, u32 scans);
u64 obssignals(Imageid owner);
int obscheck(void);
#endif
