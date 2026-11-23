/* Exercise the same tty output stage in a real user address space. */
#include "ttyuser.h"
#include "../../sbin/tty/term.h"
#include "../../sbin/tty/serial.h"
#include "private/serial.h"
#include <errno.h>
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
ttyserialdrainchecktest(void)
{
  Ttyserial       io;
  Ttyserialresult result;
  u64             target, duplicate = UINT64_MAX;
  u8              byte;
  CHECK(!serialinit(&rx, 1) && !serialinit(&tx, 1) && !ttyinputinit(&line, 1) &&
        !ttyserialinit(&io, &rx, &tx, 1));
  CHECK(serialwrite(&tx, 1, "a", 1) == 1 && !ttyserialdrainstart(&io, &target) && target == 1 &&
        ttyserialdrainstart(&io, &duplicate) == -ERR_BUSY && duplicate == UINT64_MAX &&
        ttyserialdraincheck(&io, 1, target) == -ERR_PEER);
  CHECK(serialwrite(&rx, 1, "b", 1) == 1 &&
        !ttyserialstep(&io, &line, &attr, SERIAL_BATCH, &result) && io.queued == 1 &&
        result.hints == SERIAL_RX_ROOM && !result.again && tx.producer == target &&
        ttyserialoutput(&io, attr.oflag, "c", 1) == 0);
  CHECK(serialread(&tx, 1, &byte, 1) == 1 && byte == 'a' &&
        ttyserialdraincheck(&io, 1, 0) == -ERR_WOULD_BLOCK &&
        ttyserialdraincheck(&io, 2, target) == -ERR_STALE && !ttyserialdraincheck(&io, 1, target));
  ttyserialdrainend(&io);
  CHECK(!ttyserialstep(&io, &line, &attr, SERIAL_BATCH, &result) &&
        result.hints == SERIAL_TX_READY && serialread(&tx, 1, &byte, 1) == 1 && byte == 'b');
}

static void
ttyserialendcheck(void)
{
  Ttyserial       io;
  Ttyserialresult result;
  u8              newline[TTY_BATCH];
  u64             target;
  u32             i, final;
  for (i = 0; i < ARRAY_SIZE(newline); i++)
    newline[i] = '\n';
  for (final = 0; final <= 1; final++) {
    CHECK(!serialinit(&rx, 1) && !serialinit(&tx, 1) && !ttyinputinit(&line, 1) &&
          !ttyserialinit(&io, &rx, &tx, 1));
    CHECK(ttyserialoutput(&io, attr.oflag, newline, sizeof(newline)) == sizeof(newline) &&
          !ttyserialdrainstart(&io, &target) && !target && serialwrite(&rx, 1, "a", 1) == 1 &&
          !ttyserialstep(&io, &line, &attr, SERIAL_BATCH, &result) && io.queued == 65);
    CHECK(serialwrite(&rx, 1, "\n", final) == final);
    __atomic_store_n(&rx.flags, SERIAL_EOF, __ATOMIC_RELEASE);
    CHECK(!ttyserialstep(&io, &line, &attr, SERIAL_BATCH, &result) &&
          result.end == (final ? 0 : SERIAL_EOF) && !result.hints && !result.again &&
          rx.consumer == 1 && !tx.producer && !line.ready);
    if (final) {
      ttyserialdrainend(&io);
      CHECK(!ttyserialstep(&io, &line, &attr, SERIAL_BATCH, &result) && result.again &&
            !result.end && rx.consumer == 1);
      CHECK(!ttyserialstep(&io, &line, &attr, SERIAL_BATCH, &result) && result.end == SERIAL_EOF &&
            (result.hints & SERIAL_RX_ROOM) && rx.consumer == 2 && line.ready == 2);
    }
  }
}

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
  {
    u64 producer = tx.producer;
    /* Closing RX alone still permits TX. Driver failure does not. */
    CHECK(ttyserialwrite(&io, attr.oflag, &write, page) == 2 &&
          !ttyserialstep(&io, &line, &attr, SERIAL_BATCH, &result) && !io.queued &&
          tx.producer == producer + 2);
    producer = tx.producer;
    __atomic_store_n(&rx.flags, SERIAL_ERROR, __ATOMIC_RELEASE);
    CHECK(ttyserialwrite(&io, attr.oflag, &write, page) == -ERR_STATE && !io.queued &&
          tx.producer == producer);
    __atomic_store_n(&rx.flags, SERIAL_EOF, __ATOMIC_RELEASE);
    __atomic_store_n(&tx.flags, SERIAL_ERROR, __ATOMIC_RELEASE);
    CHECK(ttyserialwrite(&io, attr.oflag, &write, page) == -ERR_STATE && !io.queued &&
          tx.producer == producer);
    write.args[1] = 0;
    CHECK(!ttyserialwrite(&io, attr.oflag, &write, page) && tx.producer == producer);
  }
}

static void
ttyserialsignalcheck(void)
{
  Ttyserial       io;
  Ttyserialresult result;
  u8              filler[SERIAL_BATCH], newline[TTY_BATCH];
  const u8        intr[] = {'z', 3, 'q'};
  u8              byte;
  u64             committed, read;
  u32             i;
  for (i = 0; i < ARRAY_SIZE(filler); i++)
    filler[i] = 'x';
  for (i = 0; i < ARRAY_SIZE(newline); i++)
    newline[i] = '\n';
  CHECK(!serialinit(&rx, 1) && !serialinit(&tx, 1) && !ttyinputinit(&line, 1) &&
        !ttyserialinit(&io, &rx, &tx, 1));
  for (i = 0; i < SERIAL_CAPACITY / SERIAL_BATCH; i++)
    CHECK(serialwrite(&tx, 1, filler, sizeof(filler)) == sizeof(filler));
  committed = tx.producer;
  CHECK(ttyserialoutput(&io, attr.oflag, newline, sizeof(newline)) == sizeof(newline) &&
        serialwrite(&rx, 1, filler, 1) == 1 &&
        !ttyserialstep(&io, &line, &attr, SERIAL_BATCH, &result) && io.queued == 65 &&
        line.used == 1 && result.hints == SERIAL_RX_ROOM);
  read = rx.consumer;
  CHECK(serialwrite(&rx, 1, intr, sizeof(intr)) == sizeof(intr) &&
        !ttyserialstep(&io, &line, &attr, SERIAL_BATCH, &result) && !result.hints &&
        !result.signal && !result.again && io.queued == 65 && rx.consumer == read &&
        tx.producer == committed);
  CHECK(serialread(&tx, 1, &byte, 1) == 1 && byte == 'x' &&
        !ttyserialstep(&io, &line, &attr, SERIAL_BATCH, &result) &&
        result.hints == SERIAL_TX_READY && result.again && io.queued == 64 && rx.consumer == read &&
        tx.producer == committed + 1);
  CHECK(!ttyserialstep(&io, &line, &attr, SERIAL_BATCH, &result) && result.signal == SIGNAL_INT &&
        result.again && result.hints == SERIAL_RX_ROOM && !line.used && !line.ready &&
        io.queued == 4 && io.output[0] == '^' && io.output[1] == 'C' && io.output[2] == '\r' &&
        io.output[3] == '\n' && rx.consumer == read + 2 && tx.producer == committed + 1);
}

static void
ttyfilereadcheck(void)
{
  Fileslot  read = {0};
  Ttyeffect effect;
  u8        page[64];
  u32       i;
  CHECK(!ttyinputinit(&line, 1));
  attr.cc[TTY_VEOF] = 4;
  read.span = read.object = 1;
  read.payload_bytes = sizeof(page);
  read.args[1] = sizeof(page) - 2;
  read.args[2] = 2;
  for (i = 0; i < sizeof(page); i++)
    page[i] = 0xa5;
  for (i = 0; i < 40; i++)
    CHECK(!ttyinputbyte(&line, &attr, 'a', &effect));
  CHECK(ttyserialread(&line, &read, page) == -ERR_WOULD_BLOCK && line.used == 40 &&
        page[2] == 0xa5);
  CHECK(!ttyinputbyte(&line, &attr, '\n', &effect) &&
        !ttyinputbyte(&line, &attr, attr.cc[TTY_VEOF], &effect));
  read.args[2]++;
  CHECK(ttyserialread(&line, &read, page) == -ERR_INVALID && line.ready == 42);
  read.args[2]--;
  CHECK(ttyserialread(&line, &read, page) == 41 && line.ready == 1 && page[1] == 0xa5 &&
        page[42] == '\n' && page[43] == 0xa5);
  for (i = 2; i < 42; i++)
    CHECK(page[i] == 'a');
  read.args[1] = 0;
  CHECK(!ttyserialread(&line, &read, NULL) && line.ready == 1);
  read.args[1] = 1;
  CHECK(!ttyserialread(&line, &read, page) && !line.ready && page[2] == 'a' &&
        ttyserialread(&line, &read, page) == -ERR_WOULD_BLOCK);
  CHECK(!ttyinputbyte(&line, &attr, 'b', &effect) && !ttyinputbyte(&line, &attr, '\n', &effect) &&
        ttyserialread(&line, &read, page) == 1 && page[2] == 'b' && line.ready == 1 &&
        ttyserialread(&line, &read, page) == 1 && page[2] == '\n' && !line.ready);
}

static void
ttytimedcheck(void)
{
  Ttyserial       io;
  Ttyserialresult result;
  Ttywait         wait;
  Termarg         raw = {0, 0, TTY_CS8, 0, 0, 0, {0}};
  u64             first, next;
  u32             ended;
  u8              bytes[2];
  raw.cc[TTY_VMIN] = 3;
  raw.cc[TTY_VTIME] = 1;
  CHECK(!serialinit(&rx, 1) && !serialinit(&tx, 1) && !ttyinputinit(&line, 0) &&
        !ttyserialinit(&io, &rx, &tx, 1) && !ttywaitinit(&wait, &raw, 3, 0, 0, 0));
  CHECK(!ttyserialtimedstep(&io, &line, &raw, &wait, 1, &next, &result) && !next);
  CHECK(serialwrite(&rx, 1, "ab", 2) == 2 &&
        !ttyserialtimedstep(&io, &line, &raw, &wait, 2, &first, &result) &&
        first == UINT64_C(100000002) && line.ready == 1);
  CHECK(!ttyserialtimedstep(&io, &line, &raw, &wait, first - 1, &next, &result) &&
        next == first - 1 + UINT64_C(100000000) && line.ready == 2);
  CHECK(serialwrite(&rx, 1, "c", 1) == 1 &&
        ttyserialtimedstep(&io, &line, &raw, &wait, next, &next, &result) == 1 && !next &&
        !result.hints && line.ready == 2 && rx.producer - rx.consumer == 1);
  CHECK(ttyinputread(&line, bytes, sizeof(bytes), &ended) == 2 && bytes[0] == 'a' &&
        bytes[1] == 'b' && !ended);
  CHECK(!ttyserialstep(&io, &line, &raw, 1, &result) &&
        ttyinputread(&line, bytes, sizeof(bytes), &ended) == 1 && bytes[0] == 'c');
}

static void
ttyopencheck(void)
{
  Openprep refs[3] = {{0}};
  u32      flags[3] = {FS_OPEN_RDONLY | FS_OPEN_NONBLOCK, FS_OPEN_RDONLY | FS_OPEN_NONBLOCK,
                       FS_OPEN_WRONLY};
  Ttyopens opens = {refs, flags, ARRAY_SIZE(refs)};
  Handle   client = (Handle) (UINT64_C(1) << 59 | UINT64_C(1) << 16 | 2);
  refs[0] = (Openprep){.client = client, .client_image = 2, .session = 3, .state = Openpending};
  CHECK(ttyopenaccess(&opens, client, 2, 3, FS_READ) == -EBADF);
  sysopencommit(&refs[0]);
  refs[1] = refs[0];
  refs[2] = refs[0];
  refs[2].client_image--;
  CHECK(ttyopenaccess(&opens, client, 2, 3, FS_READ) == 1 &&
        ttyopenaccess(&opens, client, 2, 3, FS_WRITE) == -EBADF &&
        ttyopenaccess(&opens, client, 1, 3, FS_READ) == -EBADF &&
        !ttyopenaccess(&opens, client, 1, 3, FS_WRITE));
  sysopenretire(&refs[0]);
  CHECK(ttyopenaccess(&opens, client, 2, 3, FS_TTYCTL) == 1);
  sysopenretire(&refs[1]);
  CHECK(ttyopenaccess(&opens, client, 2, 3, FS_TTYCTL) == -EBADF);
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
  ttyserialdrainchecktest();
  ttyserialendcheck();
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
  ttyserialsignalcheck();
  ttyfilereadcheck();
  ttytimedcheck();
  ttyopencheck();
}
