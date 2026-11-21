/* Exercise the same tty output stage in a real user address space. */
#include "ttyuser.h"
#include "../../sbin/tty/term.h"
#include "../../sbin/tty/serial.h"
#include "private/serial.h"
#include <unistd.h>

static Serialring rx, tx;
static Termarg    attr;
static Ttyinput   line;
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      ttytrace[7] = __LINE__;                                                                      \
      _exit(230);                                                                                  \
    }                                                                                              \
  } while (0)

static void
ttyserialcheck(void)
{
  Ttyserial       io;
  Ttyserialresult result;
  Fileslot        write = {0};
  u8              readback[SERIAL_BATCH], filler[SERIAL_BATCH];
  u8              page[] = {'x', '\n', '\n'};
  const u8        edit[] = {'a', 'b', 127, '\r'};
  const u8        echo[] = {'a', 'b', '\b', ' ', '\b', '\r', '\n'};
  const u8        intr[] = {3, 'z', '\n'};
  u32             ended, i;
  attr.cc[TTY_VERASE] = 127;
  attr.cc[TTY_VINTR] = 3;
  CHECK(!serialinit(&rx, 1) && !serialinit(&tx, 1) && !ttyinputinit(&line, 1));
  CHECK(ttyserialinit(&io, &rx, &tx, 2) == -ERR_STALE && !ttyserialinit(&io, &rx, &tx, 1));
  CHECK(serialwrite(&rx, 1, edit, sizeof(edit)) == sizeof(edit) &&
        !ttyserialstep(&io, &line, &attr, SERIAL_BATCH, &result) &&
        result.hints == (SERIAL_RX_ROOM | SERIAL_TX_READY) && !result.signal && !result.again);
  CHECK(ttyinputread(&line, readback, sizeof(readback), &ended) == 2 && ended &&
        readback[0] == 'a' && readback[1] == '\n');
  CHECK(serialread(&tx, 1, readback, sizeof(readback)) == sizeof(echo));
  for (i = 0; i < sizeof(echo); i++)
    CHECK(readback[i] == echo[i]);

  for (i = 0; i < sizeof(filler); i++)
    filler[i] = 'x';
  for (i = 0; i < SERIAL_CAPACITY / SERIAL_BATCH; i++)
    CHECK(serialwrite(&tx, 1, filler, sizeof(filler)) == sizeof(filler));
  write.object = write.span = 1;
  write.payload_bytes = sizeof(page);
  write.args[1] = 2;
  write.args[2] = 2;
  CHECK(ttyserialwrite(&io, attr.oflag, &write, page) == -ERR_INVALID && !io.queued);
  write.args[2] = 1;
  write.args[3] = FS_IO_POSITIONAL;
  CHECK(ttyserialwrite(&io, attr.oflag, &write, page) == -ERR_NOTSUP && !io.queued);
  write.args[3] = 0;
  CHECK(ttyserialwrite(&io, attr.oflag, &write, page) == 2 &&
        ttyserialwrite(&io, attr.oflag, &write, page) == -ERR_WOULD_BLOCK &&
        !ttyserialstep(&io, &line, &attr, SERIAL_BATCH, &result) && !result.hints &&
        !result.again && io.queued == 4);
  page[1] = page[2] = 'y';
  CHECK(serialread(&tx, 1, readback, 1) == 1 && readback[0] == 'x' &&
        !ttyserialstep(&io, &line, &attr, SERIAL_BATCH, &result) &&
        result.hints == SERIAL_TX_READY && io.queued == 3);
  CHECK(serialwrite(&rx, 1, intr, sizeof(intr)) == sizeof(intr) &&
        !ttyserialstep(&io, &line, &attr, SERIAL_BATCH, &result) && result.again &&
        result.signal == SIGNAL_INT && result.hints == SERIAL_RX_ROOM && io.queued == 4 &&
        tx.producer - tx.consumer == SERIAL_CAPACITY && rx.producer - rx.consumer == 2);
  for (i = 0; i < SERIAL_CAPACITY / SERIAL_BATCH; i++)
    CHECK(serialread(&tx, 1, readback, sizeof(readback)) == sizeof(readback));
  CHECK(readback[SERIAL_BATCH - 2] == 'x' && readback[SERIAL_BATCH - 1] == '\r');

  __atomic_store_n(&rx.flags, SERIAL_EOF, __ATOMIC_RELEASE);
  CHECK(!ttyserialstep(&io, &line, &attr, 1, &result) && result.again && !result.end &&
        !line.ready);
  CHECK(!ttyserialstep(&io, &line, &attr, SERIAL_BATCH, &result) && result.end == SERIAL_EOF &&
        !result.again && result.hints == (SERIAL_RX_ROOM | SERIAL_TX_READY));
  CHECK(ttyinputread(&line, readback, sizeof(readback), &ended) == 2 && ended &&
        readback[0] == 'z' && readback[1] == '\n');
  CHECK(serialread(&tx, 1, readback, sizeof(readback)) == 7 && readback[0] == '^' &&
        readback[1] == 'C' && readback[2] == '\r' && readback[3] == '\n' && readback[4] == 'z' &&
        readback[5] == '\r' && readback[6] == '\n');
}

void
ttyusercheck(void)
{
  u8    input[TTY_BATCH], output[2 * TTY_BATCH], readback[SERIAL_BATCH];
  u32   i, consumed;
  isize amount;
  ttyinputusercheck(&tx);
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
  ttyserialcheck();
}
