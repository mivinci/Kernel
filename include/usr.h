#ifndef _USR_H
#define _USR_H

void usr_init(void);
int  usr_spawn(const char *path);
void usr_enter(void) __attribute__((noreturn));
int  usr_load(const char *path, void **out_page, int *out_size);

#endif /* _USR_H */
