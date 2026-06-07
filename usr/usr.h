/* User-space mini libc: syscall wrappers and string functions */

static inline void *memset(void *p, int c, int n) {
  char *q = p;
  while (n--) *q++ = (char)c;
  return p;
}

static inline void *memcpy(void *dst, const void *src, int n) {
  char       *d = dst;
  const char *s = src;
  while (n--) *d++ = *s++;
  return dst;
}

static inline int write(int fd, const char *buf, int len) {
  register long r0 asm("a0") = fd;
  register long r1 asm("a1") = (long)buf;
  register long r2 asm("a2") = len;
  register long r7 asm("a7") = 1;  /* SYS_WRITE */
  __asm__ __volatile__("ecall" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
  return (int)r0;
}

static inline void exit(int code) {
  register long r0 asm("a0") = code;
  register long r7 asm("a7") = 2;  /* SYS_EXIT */
  __asm__ __volatile__("ecall" : "+r"(r0) : "r"(r7) : "memory");
  __builtin_unreachable();
}

static inline void yield(void) {
  register long r7 asm("a7") = 3;  /* SYS_YIELD */
  __asm__ __volatile__("ecall" : : "r"(r7) : "memory");
}

static inline int spawn(const char *path) {
  register long r0 asm("a0") = (long)path;
  register long r7 asm("a7") = 8;  /* SYS_SPAWN */
  __asm__ __volatile__("ecall" : "+r"(r0) : "r"(r7) : "memory");
  return (int)r0;
}

static inline int wait(int pid) {
  register long r0 asm("a0") = pid;
  register long r7 asm("a7") = 9;  /* SYS_WAIT */
  __asm__ __volatile__("ecall" : "+r"(r0) : "r"(r7) : "memory");
  return (int)r0;
}

static inline int read(int fd, char *buf, int len) {
  register long r0 asm("a0") = fd;
  register long r1 asm("a1") = (long)buf;
  register long r2 asm("a2") = len;
  register long r7 asm("a7") = 7;  /* SYS_READ */
  __asm__ __volatile__("ecall" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
  return (int)r0;
}
