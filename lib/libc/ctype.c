#include <ctype.h>

int
isdigit(int byte)
{
  return byte >= '0' && byte <= '9';
}

int
isupper(int byte)
{
  return byte >= 'A' && byte <= 'Z';
}

int
islower(int byte)
{
  return byte >= 'a' && byte <= 'z';
}

int
isalpha(int byte)
{
  return isupper(byte) || islower(byte);
}

int
isalnum(int byte)
{
  return isalpha(byte) || isdigit(byte);
}

int
isxdigit(int byte)
{
  return isdigit(byte) || (byte >= 'A' && byte <= 'F') || (byte >= 'a' && byte <= 'f');
}

int
isblank(int byte)
{
  return byte == ' ' || byte == '\t';
}

int
isspace(int byte)
{
  return byte == ' ' || (byte >= '\t' && byte <= '\r');
}

int
iscntrl(int byte)
{
  return (byte >= 0 && byte < ' ') || byte == 127;
}

int
isprint(int byte)
{
  return byte >= ' ' && byte <= '~';
}

int
isgraph(int byte)
{
  return byte > ' ' && byte <= '~';
}

int
ispunct(int byte)
{
  return isgraph(byte) && !isalnum(byte);
}

int
tolower(int byte)
{
  return isupper(byte) ? byte + ('a' - 'A') : byte;
}

int
toupper(int byte)
{
  return islower(byte) ? byte - ('a' - 'A') : byte;
}
