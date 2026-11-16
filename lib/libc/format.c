#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <types.h>
typedef struct Output Output;
/* LLVM's C89 dialect supports these exact ll varargs types. Substituting a
   same-width long would not satisfy the varargs type contract. */
__extension__ typedef long long          Llong;
__extension__ typedef unsigned long long Ullong;
struct Output
{
  char  *buffer;
  size_t size, used;
  int    error;
};
enum
{
  FORMAT_LEFT = 1,
  FORMAT_PLUS = 2,
  FORMAT_SPACE = 4,
  FORMAT_ALT = 8,
  FORMAT_ZERO = 16
};
enum
{
  FORMAT_DEFAULT,
  FORMAT_HH,
  FORMAT_H,
  FORMAT_L,
  FORMAT_LL,
  FORMAT_J,
  FORMAT_Z,
  FORMAT_T
};
static void
emit(Output *out, const char *text, char fill, size_t count)
{
  size_t available = 0, i;
  if (out->error)
    return;
  if (count > (size_t) INT_MAX - out->used) {
    out->error = EOVERFLOW;
    return;
  }
  if (out->size && out->used < out->size - 1)
    available = MIN(count, out->size - 1 - out->used);
  if (text)
    for (i = 0; i < available; i++)
      out->buffer[out->used + i] = text[i];
  else
    for (i = 0; i < available; i++)
      out->buffer[out->used + i] = fill;
  out->used += count;
}
static u32
flag(char byte)
{
  switch (byte) {
  case '-':
    return FORMAT_LEFT;
  case '+':
    return FORMAT_PLUS;
  case ' ':
    return FORMAT_SPACE;
  case '#':
    return FORMAT_ALT;
  case '0':
    return FORMAT_ZERO;
  default:
    return 0;
  }
}
static int
decimal(Output *out, const char **format)
{
  int value = 0, digit;
  while (**format >= '0' && **format <= '9') {
    digit = *(*format)++ - '0';
    if (value > (INT_MAX - digit) / 10) {
      out->error = EOVERFLOW;
      return 0;
    }
    value = value * 10 + digit;
  }
  return value;
}
static intmax_t
signednumber(va_list *arguments, u32 length)
{
  switch (length) {
  case FORMAT_HH:
    return (signed char) va_arg(*arguments, int);
  case FORMAT_H:
    return (short) va_arg(*arguments, int);
  case FORMAT_L:
    return va_arg(*arguments, long);
  case FORMAT_LL:
    return va_arg(*arguments, Llong);
  case FORMAT_J:
    return va_arg(*arguments, intmax_t);
  case FORMAT_Z:
  case FORMAT_T:
    return va_arg(*arguments, ptrdiff_t);
  default:
    return va_arg(*arguments, int);
  }
}
static uintmax_t
unsignednumber(va_list *arguments, u32 length)
{
  switch (length) {
  case FORMAT_HH:
    return (unsigned char) va_arg(*arguments, unsigned int);
  case FORMAT_H:
    return (unsigned short) va_arg(*arguments, unsigned int);
  case FORMAT_L:
    return va_arg(*arguments, unsigned long);
  case FORMAT_LL:
    return va_arg(*arguments, Ullong);
  case FORMAT_J:
    return va_arg(*arguments, uintmax_t);
  case FORMAT_Z:
  case FORMAT_T:
    return va_arg(*arguments, size_t);
  default:
    return va_arg(*arguments, unsigned int);
  }
}
static void
field(Output *out, const char *text, size_t count, size_t width, u32 flags)
{
  size_t padding = width > count ? width - count : 0;
  if (!(flags & FORMAT_LEFT))
    emit(out, NULL, ' ', padding);
  emit(out, text, 0, count);
  if (flags & FORMAT_LEFT)
    emit(out, NULL, ' ', padding);
}
static void
integer(Output *out, uintmax_t value, char sign, u32 base, u32 upper, u32 pointer, size_t width,
        int precision, u32 flags)
{
  char        digits[sizeof(uintmax_t) * CHAR_BIT], prefix[3], *first = digits + sizeof(digits);
  const char *alphabet = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  size_t      count, zeros = 0, prefix_bytes = 0, padding, total;
  uintmax_t   original = value;
  if (value || precision != 0 || pointer) {
    do {
      *--first = alphabet[value % base];
      value /= base;
    } while (value);
  }
  count = (size_t) (digits + sizeof(digits) - first);
  if (precision >= 0 && (size_t) precision > count)
    zeros = (size_t) precision - count;
  if ((flags & FORMAT_ALT) && base == 8 && !zeros && (!count || *first != '0'))
    zeros = 1;
  if (sign)
    prefix[prefix_bytes++] = sign;
  if (pointer || ((flags & FORMAT_ALT) && base == 16 && original)) {
    prefix[prefix_bytes++] = '0';
    prefix[prefix_bytes++] = upper ? 'X' : 'x';
  }
  total = count + zeros + prefix_bytes;
  padding = width > total ? width - total : 0;
  if (!(flags & FORMAT_LEFT) && (!(flags & FORMAT_ZERO) || precision >= 0))
    emit(out, NULL, ' ', padding);
  emit(out, prefix, 0, prefix_bytes);
  if (!(flags & FORMAT_LEFT) && (flags & FORMAT_ZERO) && precision < 0)
    emit(out, NULL, '0', padding);
  emit(out, NULL, '0', zeros);
  emit(out, first, 0, count);
  if (flags & FORMAT_LEFT)
    emit(out, NULL, ' ', padding);
}
int
vsnprintf(char *buffer, size_t size, const char *format, va_list arguments)
{
  Output      out;
  va_list     copy;
  const char *start, *text;
  size_t      width, count;
  u32         flags, next, length, base;
  int         raw, precision;
  char        conversion, byte, sign;
  intmax_t    signed_value;
  uintmax_t   value;
  out.buffer = buffer;
  out.size = size;
  out.used = 0;
  out.error = 0;
  __builtin_va_copy(copy, arguments);
  while (*format && !out.error) {
    start = format;
    while (*format && *format != '%')
      format++;
    emit(&out, start, 0, (size_t) (format - start));
    if (!*format || out.error)
      break;
    format++;
    if (*format == '%') {
      emit(&out, format++, 0, 1);
      continue;
    }
    flags = 0;
    while ((next = flag(*format)) != 0) {
      flags |= next;
      format++;
    }
    if (*format == '*') {
      raw = va_arg(copy, int);
      format++;
      if (raw < 0) {
        flags |= FORMAT_LEFT;
        width = (size_t) (0U - (unsigned int) raw);
      } else
        width = (size_t) raw;
    } else
      width = (size_t) decimal(&out, &format);
    if (out.error)
      break;
    precision = -1;
    if (*format == '.') {
      format++;
      if (*format == '*') {
        raw = va_arg(copy, int);
        format++;
        precision = raw < 0 ? -1 : raw;
      } else
        precision = decimal(&out, &format);
    }
    if (out.error)
      break;
    length = FORMAT_DEFAULT;
    if (*format == 'h') {
      length = FORMAT_H;
      if (*++format == 'h') {
        length = FORMAT_HH;
        format++;
      }
    } else if (*format == 'l') {
      length = FORMAT_L;
      if (*++format == 'l') {
        length = FORMAT_LL;
        format++;
      }
    } else if (*format == 'j' || *format == 'z' || *format == 't') {
      length = *format == 'j' ? FORMAT_J : *format == 'z' ? FORMAT_Z : FORMAT_T;
      format++;
    }
    conversion = *format;
    if (!conversion) {
      out.error = EINVAL;
      break;
    }
    format++;
    if (conversion == 's' && length == FORMAT_DEFAULT) {
      text = va_arg(copy, const char *);
      if (!text && precision != 0) {
        out.error = EINVAL;
        break;
      }
      count = 0;
      while ((precision < 0 || count < (size_t) precision) && text[count]) {
        if (count == (size_t) INT_MAX) {
          out.error = EOVERFLOW;
          break;
        }
        count++;
      }
      field(&out, text, count, width, flags);
    } else if (conversion == 'c' && length == FORMAT_DEFAULT) {
      byte = (char) va_arg(copy, int);
      field(&out, &byte, 1, width, flags);
    } else if (conversion == 'd' || conversion == 'i') {
      signed_value = signednumber(&copy, length);
      value = (uintmax_t) signed_value;
      sign = 0;
      if (signed_value < 0) {
        sign = '-';
        value = 0 - value;
      } else if (flags & FORMAT_PLUS)
        sign = '+';
      else if (flags & FORMAT_SPACE)
        sign = ' ';
      integer(&out, value, sign, 10, 0, 0, width, precision, flags);
    } else if (conversion == 'u' || conversion == 'o' || conversion == 'x' || conversion == 'X') {
      value = unsignednumber(&copy, length);
      base = conversion == 'o' ? 8 : conversion == 'u' ? 10 : 16;
      integer(&out, value, 0, base, conversion == 'X', 0, width, precision, flags);
    } else if (conversion == 'p' && length == FORMAT_DEFAULT) {
      value = (uintptr_t) va_arg(copy, void *);
      integer(&out, value, 0, 16, 0, 1, width, precision, flags);
    } else
      out.error = EINVAL;
  }
  va_end(copy);
  if (size)
    buffer[MIN(out.used, size - 1)] = 0;
  if (out.error) {
    errno = out.error;
    return -1;
  }
  return (int) out.used;
}
int
snprintf(char *buffer, size_t size, const char *format, ...)
{
  va_list arguments;
  int     result;
  va_start(arguments, format);
  result = vsnprintf(buffer, size, format, arguments);
  va_end(arguments);
  return result;
}
