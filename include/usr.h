#ifndef _USR_H
#define _USR_H

void usr_init(void);
int  usr_spawn(void);
void usr_enter(unsigned long entry) __attribute__((noreturn));

#endif /* _USR_H */
