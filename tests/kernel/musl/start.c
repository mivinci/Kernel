#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>
#include "pthread_impl.h"
#include "libc.h"
struct Boot;
struct Bootview;
extern long                  sysfilebootinstall(const struct Bootview *, void *);
extern long                  sysstartargswithfds(const struct Boot *, int *, char ***, char ***,
                                                 long (*)(const struct Bootview *, void *));
extern void                  __sys_exit(unsigned long) __attribute__((noreturn));
extern const size_t          __native_tls[4];
static struct tls_module     tls;
static unsigned char         storage[2][4096] __attribute__((aligned(64)));
static __thread int          initialized = 42;
static __thread volatile int zero __attribute__((aligned(64)));
/* A kernel fixture can seed this before publishing the image. */
volatile unsigned long musl_probe_role;
volatile unsigned long musl_probe_turn;
extern char            __image_end[];
extern long            sysfileheapinit(unsigned long, unsigned long);
extern long            sysvmget(unsigned long, void **), sysvmgetguarded(unsigned long, void **),
    sysvmput(void *);
static size_t aux_end[2];
static void  *meta, *large_map;
extern long   __sys_copy(void *, const void *, unsigned long);
extern long   __sys_yield(void);
void *
native_meta(size_t n)
{
  void *p;
  if (sysvmgetguarded(n, &p)) {
    errno = ENOMEM;
    return 0;
  }
  meta = p;
  return p;
}
void *
__mmap(void *a, size_t n, int p, int f, int fd, off_t off)
{
  void *out;
  if (a || !n || n > SIZE_MAX - 4095 || p != (PROT_READ | PROT_WRITE) ||
      f != (MAP_PRIVATE | MAP_ANON) || fd != -1 || off) {
    errno = EINVAL;
    return MAP_FAILED;
  }
  if (sysvmget((n + 4095) & ~4095UL, &out)) {
    errno = ENOMEM;
    return MAP_FAILED;
  }
  if (n >= 131072)
    large_map = out;
  return out;
}
int
munmap(void *p, size_t n)
{
  if (!n || sysvmput(p)) {
    errno = EINVAL;
    return -1;
  }
  return 0;
}
void
csustart(const struct Boot *boot)
{
  int       argc;
  char    **argv, **envp;
  pthread_t td;
  size_t    align = __native_tls[3];
  if (sysstartargswithfds(boot, &argc, &argv, &envp, sysfilebootinstall) || align > 64 || !align ||
      __native_tls[2] > 2048)
    __sys_exit(120);
  tls.image = (void *) __native_tls[0];
  tls.len = __native_tls[1];
  tls.size = __native_tls[2];
  tls.align = align;
  tls.offset = (GAP_ABOVE_TP + align - 1) & -align;
  libc.tls_head = &tls;
  libc.tls_cnt = 1;
  libc.tls_align = align;
  libc.tls_size = sizeof storage[0];
  td = __copy_tls(storage[musl_probe_role == 2]);
  td->self = td;
  if (__set_thread_area(TP_ADJ(td)) || __pthread_self() != td)
    __sys_exit(121);
  if (initialized != 42 || zero || errno || (argc && !argv[0]) || !envp || musl_probe_role > 2)
    __sys_exit(122);
  {
    int role = (int) musl_probe_role;
    int turn;
    initialized = 43 + role;
    errno = 77 + role;
    zero = 33 + role;
    for (turn = 0; turn < 4; turn++) {
      musl_probe_turn = turn + 1;
      if (__sys_yield() || __pthread_self() != td)
        __sys_exit(129);
      if (initialized != 43 + role || zero != 33 + role || errno != 77 + role)
        __sys_exit(123);
    }
  }
  {
    volatile unsigned char *a, *b;
    libc.auxv = aux_end;
    libc.page_size = 4096;
    if (sysfileheapinit((size_t) __image_end, 0x3ffe000000UL))
      __sys_exit(124);
    a = malloc(32);
    b = malloc(131072);
    if (!a || !b)
      __sys_exit(125);
    a[31] = 13;
    b[0] = 44;
    b[131071] = 17;
    if (a[31] != 13 || b[0] != 44 || b[131071] != 17)
      __sys_exit(126);
    free((void *) a);
    free((void *) b);
    unsigned char byte = 0xff;
    if (!meta || !large_map || !__sys_copy(&byte, (char *) meta - 4096, 1) || byte != 0xff ||
        !__sys_copy(&byte, large_map, 1) || byte != 0xff)
      __sys_exit(127);
    if (sysvmput(meta) || !__sys_copy(&byte, meta, 1))
      __sys_exit(128);
  }
#ifdef MUSL_PROBE_INHERITED_STDOUT
  errno = 0;
  if (fwrite("native stdout", 1, 13, stdout) != 13)
    __sys_exit(140 + errno);
  if (fflush(stdout) || ferror(stdout))
    __sys_exit(175 + errno);
  {
    int   status;
    pid_t child = fork();
    if (!child) {
      unsigned char byte = 0;
      if (dup2(1, 0) != 0)
        __sys_exit(137);
      __sys_exit(read(0, &byte, 1) == 1 && byte == 0x5a ? 43 : 135);
    }
    if (child < 0)
      __sys_exit(140 + errno);
    if (waitpid(child, &status, 0) != child)
      __sys_exit(175 + errno);
    if (status != (43 << 8))
      __sys_exit(136);
  }
#else
  {
    int fds[2] = {-1, -1};
    if (pipe(fds) != -1 || errno != ENOSYS || fds[0] != -1 || fds[1] != -1)
      __sys_exit(138);
  }
  if (fwrite("native stdout", 1, 13, stdout) != 13 || fflush(stdout) != EOF || !ferror(stdout) ||
      errno != EBADF)
    __sys_exit(130);
  errno = 0;
  {
    pid_t child = fork();
    if (musl_probe_role == 1) {
      int status;
      if (!child) {
        unsigned char byte = 0x5a;
        if (initialized != 44 || zero != 34)
          __sys_exit(132);
        errno = 0;
        if (read(0, &byte, sizeof(byte)) != -1 || errno != EBADF || byte != 0x5a)
          __sys_exit(135);
        __sys_exit(43);
      }
      if (child < 0)
        __sys_exit(140 + errno);
      if (waitpid(child, &status, 0) != child)
        __sys_exit(175 + errno);
      if (status != (43 << 8))
        __sys_exit(133);
      if (waitpid(child, NULL, 0) != -1 || errno != ECHILD)
        __sys_exit(134);
    } else if (child != -1 || errno != EPERM) {
      __sys_exit(131);
    }
  }
#endif
  __sys_exit(42);
}
