#ifndef _EVENT_H
#define _EVENT_H
#include <types.h>
#include <abi/event.h>
typedef struct Event Event;
struct Event
{
  u64 pending, deadline, generation;
  u32 armed, prefermessage;
};
STATIC_ASSERT(sizeof(Event) == 32, event_size);
void eventinit(Event *event);
void eventpost(Event *event, u64 bits);
/* Select without consuming; 0 none, 1 EVENT, 2 message. */
u32  eventchoose(const Event *event, u64 mask, u32 message, u32 only);
void eventserved(Event *event, u32 choice, u32 only);
void eventtake(Event *event, u64 mask, u64 *bits, u64 *generation);
int  eventalarm(Event *event, u64 deadline, u64 now, u64 *generation);
int  eventcancel(Event *event, u64 *generation);
int  eventexpire(Event *event, u64 now);
#endif
