#include "ttyuser.h"
#include "../../sbin/tty/input.h"
#include "private/serial.h"
#include <unistd.h>

static Ttyinput input;
static Termarg  mode;
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c))                                                                                      \
      _exit(231);                                                                                  \
  } while (0)

void
ttyinputusercheck(Serialring *rx)
{
  static const u8 text[] = {'a', 'b', 127, 'c', '\r', 4, 0, 255, 4};
  Ttyeffect       effect;
  u8              buffer[TTY_BATCH], encoded[2 * TTY_BATCH];
  u32             i, count, ended;
  isize           amount;
  mode.iflag = TTY_ICRNL;
  mode.oflag = TTY_OPOST | TTY_ONLCR;
  mode.cflag = TTY_CS8;
  mode.lflag = TTY_ICANON | TTY_ECHO | TTY_ISIG;
  mode.cc[TTY_VINTR] = 3;
  mode.cc[TTY_VQUIT] = 28;
  mode.cc[TTY_VERASE] = 127;
  mode.cc[TTY_VKILL] = 21;
  mode.cc[TTY_VEOF] = 4;
  mode.cc[TTY_VSUSP] = 26;
  CHECK(!ttyinputinit(&input, 1) && !serialinit(rx, 1));
  CHECK(serialwrite(rx, 1, text, sizeof(text)) == sizeof(text));
  CHECK(serialread(rx, 1, buffer, sizeof(buffer)) == sizeof(text));
  for (i = 0; i < sizeof(text); i++) {
    CHECK(!ttyinputbyte(&input, &mode, buffer[i], &effect));
    if (i == 2)
      CHECK(effect.bytes == 3 && effect.echo[0] == '\b' && effect.echo[1] == ' ' &&
            effect.echo[2] == '\b');
    if (i == 4) {
      CHECK(effect.bytes == 1 && effect.echo[0] == '\n');
      CHECK(ttyencode(mode.oflag, effect.echo, effect.bytes, encoded, sizeof(encoded), &count) ==
                2 &&
            count == 1 && encoded[0] == '\r' && encoded[1] == '\n');
    }
  }
  CHECK(ttyinputread(&input, buffer, 1, &ended) == 1 && buffer[0] == 'a' && !ended);
  CHECK(ttyinputread(&input, buffer, sizeof(buffer), &ended) == 2 && buffer[0] == 'c' &&
        buffer[1] == '\n' && ended);
  CHECK(!ttyinputread(&input, buffer, sizeof(buffer), &ended) && ended);
  CHECK(ttyinputread(&input, buffer, sizeof(buffer), &ended) == 2 && !buffer[0] &&
        buffer[1] == 255 && ended);
  CHECK(ttyinputread(&input, buffer, sizeof(buffer), &ended) == -ERR_WOULD_BLOCK);
  for (i = 0; i < TTY_LINE_BYTES - 1; i++)
    CHECK(!ttyinputbyte(&input, &mode, 'x', &effect) && !effect.dropped);
  CHECK(!ttyinputbyte(&input, &mode, 'y', &effect) && effect.dropped && input.dropped == 1);
  CHECK(!ttyinputbyte(&input, &mode, '\n', &effect));
  count = 0;
  while ((amount = ttyinputread(&input, buffer, sizeof(buffer), &ended)) > 0) {
    for (i = 0; i < (u32) amount; i++)
      CHECK(buffer[i] == (count + i == TTY_LINE_BYTES - 1 ? '\n' : 'x'));
    count += (u32) amount;
    CHECK(ended == (count == TTY_LINE_BYTES));
  }
  CHECK(amount == -ERR_WOULD_BLOCK && count == TTY_LINE_BYTES);
  CHECK(!ttyinputbyte(&input, &mode, 'q', &effect));
  CHECK(!ttyinputbyte(&input, &mode, 21, &effect) && effect.bytes == 1 && effect.echo[0] == '\n' &&
        !input.used);
  CHECK(!ttyinputbyte(&input, &mode, 'q', &effect));
  CHECK(!ttyinputbyte(&input, &mode, '\n', &effect));
  CHECK(!ttyinputbyte(&input, &mode, 'p', &effect));
  CHECK(!ttyinputbyte(&input, &mode, 3, &effect) && effect.signal == SIGNAL_INT &&
        effect.flags == TTY_FLUSH_OUTPUT && !input.used && !input.ready);
  CHECK(!ttyinputbyte(&input, &mode, 26, &effect) && effect.signal == SIGNAL_TSTP);
  CHECK(!ttyinputbyte(&input, &mode, 28, &effect) && effect.signal == SIGNAL_QUIT);
  CHECK(!ttyinputinit(&input, 0));
  mode.iflag = mode.lflag = 0;
  for (i = 0; i < 256; i++) {
    CHECK(!ttyinputbyte(&input, &mode, (u8) i, &effect) && !effect.signal && !effect.bytes);
    CHECK(ttyinputread(&input, buffer, sizeof(buffer), &ended) == 1 && buffer[0] == (u8) i &&
          !ended);
  }
  CHECK(!ttyinputflush(&input) && !input.used && !input.ready);
  for (i = 0; i < sizeof(text); i++)
    CHECK(!ttyinputbyte(&input, &mode, text[i], &effect));
  CHECK(!ttyinputmode(&input, 1));
  mode.lflag = TTY_ICANON;
  CHECK(!ttyinputbyte(&input, &mode, 'p', &effect));
  CHECK(ttyinputread(&input, buffer, sizeof(buffer), &ended) == sizeof(text) && ended);
  for (i = 0; i < sizeof(text); i++)
    CHECK(buffer[i] == text[i]);
  CHECK(ttyinputread(&input, buffer, sizeof(buffer), &ended) == -ERR_WOULD_BLOCK);
  CHECK(!ttyinputbyte(&input, &mode, '\n', &effect) &&
        !ttyinputbyte(&input, &mode, mode.cc[TTY_VEOF], &effect) &&
        !ttyinputbyte(&input, &mode, 'q', &effect));
  CHECK(!ttyinputmode(&input, 0));
  CHECK(ttyinputread(&input, buffer, sizeof(buffer), &ended) == 3 && !ended && buffer[0] == 'p' &&
        buffer[1] == '\n' && buffer[2] == 'q');
  CHECK(ttyinputread(&input, buffer, sizeof(buffer), &ended) == -ERR_WOULD_BLOCK);
}
