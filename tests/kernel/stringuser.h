#ifndef _TEST_STRINGUSER_H
#define _TEST_STRINGUSER_H
#include <string.h>
#include <errno.h>
#include <sys/boot.h>
#include "private/syscall.h"
#define STRINGCHECK(c)                                                                             \
  do {                                                                                             \
    if (!(c) || errno != EDOM)                                                                     \
      _exit(220 + (int) count);                                                                    \
    count++;                                                                                       \
  } while (0)
static u32
stringusercheck(const Bootview *boot)
{
  char       buffer[24];
  const char repeated[] = "ababa";
  const char high[] = "\200\377!";
  const u8   binary[4] = {1, 0, 255, 2};
  u32        count = 0;
  errno = EDOM;
  STRINGCHECK(!strncmp("ab", "ac", 1) && strncmp("ab", "ac", 2) < 0 &&
              strncmp("\377", "a", 1) > 0 && !strncmp("a", "z", 0));
  STRINGCHECK(strnlen("abc", 2) == 2 && strnlen("abc", 9) == 3 && !strnlen("", 9));
  STRINGCHECK(strrchr(repeated, 'a') == repeated + 4 && strrchr(repeated, 0) == repeated + 5 &&
              !strrchr(repeated, 'z'));
  STRINGCHECK(strstr(repeated, "") == repeated && strstr(repeated, "aba") == repeated &&
              strstr(repeated, "bab") == repeated + 1);
  STRINGCHECK(!strstr(repeated, "ababab") && !strstr(repeated, "xyz"));
  STRINGCHECK(strspn("abba!", "ab") == 4 && !strspn("x", "") && strspn(high, "\200\377") == 2);
  STRINGCHECK(strcspn("abc!", "!") == 3 && strcspn("abc", "") == 3 && strcspn(high, "\377") == 1);
  STRINGCHECK(strpbrk(repeated, "bx") == repeated + 1 && !strpbrk(repeated, ""));
  STRINGCHECK(memchr(binary, 0, 4) == binary + 1 && memchr(binary, -1, 4) == binary + 2 &&
              !memchr(binary, 3, 4) && !memchr(binary, 1, 0));
  memset(buffer, 0xa5, sizeof(buffer));
  STRINGCHECK(strcpy(buffer + 1, "abc") == buffer + 1 && !memcmp(buffer + 1, "abc", 4) &&
              (u8) buffer[0] == 0xa5 && (u8) buffer[5] == 0xa5);
  memset(buffer, 0xa5, sizeof(buffer));
  STRINGCHECK(strncpy(buffer + 1, "abcd", 2) == buffer + 1 && !memcmp(buffer + 1, "ab", 2) &&
              (u8) buffer[3] == 0xa5);
  STRINGCHECK(strncpy(buffer + 1, "x", 6) == buffer + 1 && !memcmp(buffer + 1, "x\0\0\0\0\0", 6) &&
              (u8) buffer[7] == 0xa5);
  STRINGCHECK(strncpy(buffer, "unused", 0) == buffer && (u8) buffer[0] == 0xa5);
  strcpy(buffer, "ab");
  STRINGCHECK(strcat(buffer, "cd") == buffer && !strcmp(buffer, "abcd"));
  memset(buffer, 0xa5, sizeof(buffer));
  strcpy(buffer, "x");
  STRINGCHECK(strncat(buffer, "abcd", 2) == buffer && !strcmp(buffer, "xab") &&
              (u8) buffer[4] == 0xa5);
  STRINGCHECK(strncat(buffer, "unused", 0) == buffer && !strcmp(buffer, "xab"));
  if (!bootresource(boot, BOOT_CREATE)) {
    Allocarg allocation;
    Maparg   mapping;
    Handle   memory, space = bootresource(boot, BOOT_SELF_SPACE);
    char    *end = (char *) (uintptr_t) (0x2800000UL + PAGE_SIZE);
    allocation.bytes = PAGE_SIZE;
    allocation.kind = ALLOC_DATA;
    allocation.align = allocation.minaddr = allocation.maxaddr = allocation.reserved = 0;
    if (__sys_alloc(bootresource(boot, BOOT_SELF_ALLOC), &allocation, &memory))
      _exit(245);
    mapping.mem = memory;
    mapping.address = 0x2800000UL;
    mapping.offset = 0;
    mapping.length = PAGE_SIZE;
    mapping.prot = MAP_READ | MAP_WRITE;
    mapping.reserved = 0;
    if (__sys_map(space, &mapping))
      _exit(246);
    end[-2] = 'X';
    end[-1] = 'Y';
    STRINGCHECK(strnlen(end - 2, 2) == 2 && !strncmp(end - 2, "XY", 2));
    STRINGCHECK(memchr(end - 1, 'Y', SIZE_MAX) == end - 1);
    end[-1] = 0;
    STRINGCHECK(!strncmp(end - 2, "X", SIZE_MAX) && strnlen(end - 2, SIZE_MAX) == 1);
    STRINGCHECK(strrchr(end - 2, 0) == end - 1 && strstr(end - 2, "X") == end - 2 &&
                !strstr(end - 2, "XY"));
    STRINGCHECK(strspn(end - 2, "X") == 1 && strcspn(end - 2, "Y") == 1 &&
                strpbrk(end - 2, "X") == end - 2);
    STRINGCHECK(strncpy(end - 2, "abcd", 2) == end - 2 && !memcmp(end - 2, "ab", 2));
    STRINGCHECK(strcpy(end - 2, "X") == end - 2 && end[-2] == 'X' && !end[-1]);
    end[-3] = 'X';
    end[-2] = 0;
    STRINGCHECK(strncat(end - 3, "abc", 1) == end - 3 && !strcmp(end - 3, "Xa"));
    if (__sys_unmap(space, mapping.address) || __sys_free(memory))
      _exit(247);
  }
  return count;
}
#undef STRINGCHECK
#endif
