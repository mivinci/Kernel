/* Compile with the port's musl internal headers and without stack protection. */
#include "pthread_impl.h"
#include "libc.h"
#include <stdlib.h>

struct Boot;
struct Bootview;
extern long sysstartargswithfds(const struct Boot *, int *, char ***, char ***,
                                long (*)(const struct Bootview *, void *));
extern long sysfilebootinstall(const struct Bootview *, void *);
extern long sysfileheapinit(unsigned long, unsigned long);
extern void __sys_exit(unsigned long) __attribute__((noreturn));
extern const size_t __native_tls[4];
extern char __image_end[];
extern char **__environ;
extern int main(int, char **, char **);
extern void (*const __init_array_start[])(void), (*const __init_array_end[])(void);

static struct tls_module tls;
static _Alignas(64) unsigned char initial_tls[4096];
static size_t auxv[] = {6, 4096, 0, 0}; /* AT_PAGESZ, AT_NULL */

void __attribute__((noreturn))
csustart(const struct Boot *boot)
{
  int argc;
  char **argv, **envp;
  pthread_t td;
  size_t align = __native_tls[3], i;
  if (sysstartargswithfds(boot, &argc, &argv, &envp, sysfilebootinstall) || align > 64 ||
      (align && (align & (align - 1))) || __native_tls[2] > 2048)
    __sys_exit(127);
  if (!align)
    align = 1;
  tls.image = (void *) __native_tls[0];
  tls.len = __native_tls[1];
  tls.size = __native_tls[2];
  tls.align = align;
  tls.offset = (GAP_ABOVE_TP + align - 1) & -align;
  libc.tls_head = &tls;
  libc.tls_cnt = 1;
  libc.tls_align = align;
  libc.tls_size = sizeof(initial_tls);
  td = __copy_tls(initial_tls);
  td->self = td;
  td->locale = &libc.global_locale;
  if (__set_thread_area(TP_ADJ(td)) || sysfileheapinit((size_t) __image_end, 0x3ffe000000UL))
    __sys_exit(127);
  libc.page_size = 4096;
  libc.auxv = auxv;
  __environ = envp;
  for (i = 0; __init_array_start + i < __init_array_end; i++)
    __init_array_start[i]();
  _Exit(main(argc, argv, envp));
}
