#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
typedef struct Integer Integer;
struct Integer
{
  unsigned long value;
  u32           negative, overflow;
};
static u32
digit(u8 byte)
{
  if (byte >= '0' && byte <= '9')
    return (u32) (byte - '0');
  if (byte >= 'a' && byte <= 'z')
    return (u32) (byte - 'a') + 10;
  if (byte >= 'A' && byte <= 'Z')
    return (u32) (byte - 'A') + 10;
  return 36;
}
static Integer
convert(const char *string, char **end, int base, u32 signed_value)
{
  const char   *cursor = string, *first;
  Integer       result;
  unsigned long limit, cutoff, remainder;
  u32           value;
  result.value = result.negative = result.overflow = 0;
  if (end)
    *end = (char *) string;
  if (base && (base < 2 || base > 36)) {
    errno = EINVAL;
    return result;
  }
  while (isspace((u8) *cursor))
    cursor++;
  if (*cursor == '-' || *cursor == '+') {
    result.negative = *cursor == '-';
    cursor++;
  }
  /* A prefix must have a following digit; "0x" still converts its initial 0. */
  if ((!base || base == 16) && cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X') &&
      digit((u8) cursor[2]) < 16) {
    base = 16;
    cursor += 2;
  }
  if (!base)
    base = *cursor == '0' ? 8 : 10;
  limit = signed_value ? (unsigned long) LONG_MAX + result.negative : ULONG_MAX;
  cutoff = limit / (unsigned long) base;
  remainder = limit % (unsigned long) base;
  first = cursor;
  while ((value = digit((u8) *cursor)) < (u32) base) {
    if (result.value > cutoff || (result.value == cutoff && value > remainder))
      result.overflow = 1;
    else if (!result.overflow)
      result.value = result.value * (unsigned long) base + value;
    cursor++;
  }
  if (cursor != first && end)
    *end = (char *) cursor;
  if (result.overflow) {
    result.value = limit;
    errno = ERANGE;
  }
  return result;
}
long
strtol(const char *string, char **end, int base)
{
  Integer result = convert(string, end, base, 1);
  if (result.negative && result.value == (unsigned long) LONG_MAX + 1UL)
    return LONG_MIN;
  return result.negative ? -(long) result.value : (long) result.value;
}
unsigned long
strtoul(const char *string, char **end, int base)
{
  Integer result = convert(string, end, base, 0);
  return result.negative && !result.overflow ? 0UL - result.value : result.value;
}
