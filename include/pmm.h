#ifndef _PMM_H
#define _PMM_H

#define PAGE_SIZE 4096

void  pmm_init(void);
void *kalloc(void);
void  kfree(void *page);

#endif /* _PMM_H */
