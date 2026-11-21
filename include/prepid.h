#ifndef _PREPID_H
#define _PREPID_H
#include <types.h>
typedef struct Prepid Prepid;
struct Prepid
{
  u64 generation;
  u32 index, reserved;
};
typedef int (*Forkcheck)(Prepid);
#endif
