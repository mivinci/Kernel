#include <event.h>
#include <abi/base.h>
void
eventinit(Event *event)
{
  event->pending = event->deadline = event->generation = 0;
  event->armed = event->prefermessage = 0;
}
void
eventpost(Event *event, u64 bits)
{
  event->pending |= bits;
}
u32
eventchoose(const Event *event, u64 mask, u32 message, u32 only)
{
  u32 pending = (event->pending & mask) != 0;
  if (only)
    return pending ? 1 : 0;
  if (pending && message)
    return event->prefermessage ? 2 : 1;
  return pending ? 1 : message ? 2 : 0;
}
void
eventserved(Event *event, u32 choice, u32 only)
{
  if (!only)
    event->prefermessage = choice == 1;
}
void
eventtake(Event *event, u64 mask, u64 *bits, u64 *generation)
{
  *bits = event->pending & mask;
  *generation = *bits & EVENT_TIMER ? event->generation : 0;
  event->pending &= ~*bits;
}
int
eventexpire(Event *event, u64 now)
{
  if (!event->armed || now < event->deadline)
    return 0;
  event->armed = 0;
  event->pending |= EVENT_TIMER;
  return 1;
}
int
eventalarm(Event *event, u64 deadline, u64 now, u64 *generation)
{
  if (event->generation == ~(u64) 0)
    return -ERR_OVERFLOW;
  event->generation++;
  event->pending &= ~EVENT_TIMER;
  event->deadline = deadline;
  event->armed = 1;
  eventexpire(event, now);
  *generation = event->generation;
  return 0;
}
int
eventcancel(Event *event, u64 *generation)
{
  if (event->generation == ~(u64) 0)
    return -ERR_OVERFLOW;
  event->generation++;
  event->pending &= ~EVENT_TIMER;
  event->armed = 0;
  event->deadline = 0;
  *generation = event->generation;
  return 0;
}
