#ifndef _TEST_TTYUSER_H
#define _TEST_TTYUSER_H
#include <proto/serial.h>
extern volatile u64 ttytrace[8];
void                ttyusercheck(void);
void                ttyinputusercheck(Serialring *ring);
#endif
