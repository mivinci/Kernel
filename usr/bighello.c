/* Multi-sector test: one big message that spans sectors.
   Avoids array-of-pointer issue (VMA-relative pointers break
   when loaded at arbitrary physical address). */
#include "usr.h"

void main(void) {
  const char *msg =
    "Line 01: abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHI\n"
    "Line 02: The quick brown fox jumps over the lazy dog. 123\n"
    "Line 03: 0000000000000000000000000000000000000000000000\n"
    "Line 04: 1111111111111111111111111111111111111111111111\n"
    "Line 05: 2222222222222222222222222222222222222222222222\n"
    "Line 06: 3333333333333333333333333333333333333333333333\n"
    "Line 07: 4444444444444444444444444444444444444444444444\n"
    "Line 08: 5555555555555555555555555555555555555555555555\n"
    "Line 09: 6666666666666666666666666666666666666666666666\n"
    "Line 10: 7777777777777777777777777777777777777777777777\n"
    "Line 11: 8888888888888888888888888888888888888888888888\n"
    "Line 12: 9999999999999999999999999999999999999999999999\n"
    "Line 13: C makes it easy to shoot yourself in the foot.\n"
    "Line 14: Bjarne Stroustrup.  Hello from a multi-sector\n"
    "         binary loaded via diskfs!\n"
    "EOF\n";
  write(1, msg, 850);
  exit(0);
}
