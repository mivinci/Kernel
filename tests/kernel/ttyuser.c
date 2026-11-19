/* Exercise the same tty output stage in a real user address space. */
#include "ttyuser.h"
#include "../../sbin/tty/term.h"
#include "private/serial.h"
#include <unistd.h>

static Serialring tx;
static Termarg    attr;
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c))                                                                                      \
      _exit(230);                                                                                  \
  } while (0)

void
ttyusercheck(void)
{
  u8    input[TTY_BATCH], output[2 * TTY_BATCH], readback[SERIAL_BATCH];
  u32   i, consumed;
  isize amount;
  attr.iflag = TTY_ICRNL;
  attr.oflag = TTY_OPOST | TTY_ONLCR;
  attr.cflag = TTY_CS8;
  attr.lflag = TTY_ICANON | TTY_ECHO | TTY_ISIG;
  attr.ispeed = attr.ospeed = 115200;
  CHECK(!ttyattrcheck(&attr, 115200));
  attr.cc[31] = 1;
  CHECK(ttyattrcheck(&attr, 115200) == -ERR_NOTSUP);
  attr.cc[31] = 0;
  CHECK(ttyattrcheck(&attr, 9600) == -ERR_NOTSUP);
  for (i = 0; i < TTY_BATCH; i++)
    input[i] = '\n';
  consumed = 99;
  CHECK(!ttyencode(attr.oflag, input, TTY_BATCH, output, 1, &consumed) && !consumed);
  CHECK(ttyencode(attr.oflag, input, TTY_BATCH, output, 63, &consumed) == 62 && consumed == 31);
  for (i = 0; i < 62; i++)
    CHECK(output[i] == (i & 1 ? '\n' : '\r'));
  CHECK(!serialinit(&tx, 1));
  for (i = 0; i < SERIAL_CAPACITY / SERIAL_BATCH; i++)
    CHECK(serialwrite(&tx, 1, input, SERIAL_BATCH) == SERIAL_BATCH);
  CHECK(!serialwrite(&tx, 1, output, SERIAL_BATCH));
  CHECK(serialread(&tx, 1, readback, 7) == 7);
  CHECK(serialwrite(&tx, 1, output, SERIAL_BATCH) == 7);
  /* Ring consumption only frees room; the stage retains its unsent suffix. */
  for (i = 0; i < SERIAL_CAPACITY - 7; i += (u32) amount) {
    amount = serialread(&tx, 1, readback, MIN(SERIAL_BATCH, SERIAL_CAPACITY - 7 - i));
    CHECK(amount > 0);
  }
  CHECK(serialread(&tx, 1, readback, SERIAL_BATCH) == 7);
  for (i = 0; i < 7; i++)
    CHECK(readback[i] == output[i]);
  CHECK(serialwrite(&tx, 1, output + 7, SERIAL_BATCH) == SERIAL_BATCH);
  CHECK(serialwrite(&tx, 1, output + 39, 23) == 23);
  CHECK(serialread(&tx, 1, readback, SERIAL_BATCH) == SERIAL_BATCH);
  for (i = 0; i < SERIAL_BATCH; i++)
    CHECK(readback[i] == output[i + 7]);
  CHECK(serialread(&tx, 1, readback, SERIAL_BATCH) == 23);
  for (i = 0; i < 23; i++)
    CHECK(readback[i] == output[i + 39]);
  CHECK(ttyencode(attr.oflag, input + consumed, 1, output, sizeof(output), &consumed) == 2 &&
        consumed == 1 && output[0] == '\r' && output[1] == '\n');
  CHECK(ttyencode(TTY_ONLCR, input, 3, output, 3, &consumed) == 3 && consumed == 3 &&
        output[0] == '\n' && output[1] == '\n' && output[2] == '\n');
  consumed = 99;
  CHECK(ttyencode(8, input, 1, output, sizeof(output), &consumed) == -ERR_NOTSUP && consumed == 99);
}
