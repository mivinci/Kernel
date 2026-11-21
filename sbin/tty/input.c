#include "input.h"
#define INPUT_END 0x100
#define INPUT_EOF 0x200

static int
valid(const Ttyinput *input)
{
  return input && IS_ALIGNED((usize) input, 8) && input->canonical <= 1 &&
         input->head < TTY_INPUT_CAPACITY && input->used <= TTY_INPUT_CAPACITY &&
         input->ready <= input->used &&
         (input->canonical ? input->used - input->ready < TTY_LINE_BYTES
                           : input->ready == input->used);
}

isize
ttyinputinit(Ttyinput *input, u32 canonical)
{
  u32 i;
  if (!input || !IS_ALIGNED((usize) input, 8) || canonical > 1)
    return -ERR_INVALID;
  input->dropped = 0;
  input->head = input->used = input->ready = 0;
  input->canonical = canonical;
  for (i = 0; i < TTY_INPUT_CAPACITY; i++)
    input->data[i] = 0;
  return 0;
}

isize
ttyinputflush(Ttyinput *input)
{
  if (!valid(input))
    return -ERR_STATE;
  input->head = input->used = input->ready = 0;
  return 0;
}

static void
append(Ttyinput *input, u16 token)
{
  input->data[(input->head + input->used) % TTY_INPUT_CAPACITY] = token;
  input->used++;
}

static int
matches(u8 byte, u8 control)
{
  return control && byte == control;
}

static void
drop(Ttyinput *input, Ttyeffect *effect)
{
  if (input->dropped != UINT64_MAX)
    input->dropped++;
  effect->dropped = 1;
}

isize
ttyinputbyte(Ttyinput *input, const Termarg *mode, u8 byte, Ttyeffect *effect)
{
  Ttyeffect result;
  u32       line, i;
  isize     error;
  if (!input || !mode || !effect || !IS_ALIGNED((usize) mode, 8) || !IS_ALIGNED((usize) effect, 4))
    return -ERR_INVALID;
  if (!valid(input))
    return -ERR_STATE;
  error = ttyattrcheck(mode, mode->ispeed);
  if (error)
    return error;
  if (input->canonical != !!(mode->lflag & TTY_ICANON))
    return -ERR_STATE;
  result.bytes = result.signal = result.flags = result.dropped = 0;
  for (i = 0; i < ARRAY_SIZE(result.echo); i++)
    result.echo[i] = 0;
  if (byte == '\r' && mode->iflag & TTY_ICRNL)
    byte = '\n';
  if (mode->lflag & TTY_ISIG) {
    if (matches(byte, mode->cc[TTY_VINTR]))
      result.signal = SIGNAL_INT;
    else if (matches(byte, mode->cc[TTY_VQUIT]))
      result.signal = SIGNAL_QUIT;
    else if (matches(byte, mode->cc[TTY_VSUSP]))
      result.signal = SIGNAL_TSTP;
  }
  if (result.signal) {
    input->head = input->used = input->ready = 0;
    result.flags = TTY_FLUSH_OUTPUT;
    if (mode->lflag & TTY_ECHO) {
      if (byte < 32) {
        result.echo[result.bytes++] = '^';
        result.echo[result.bytes++] = byte + '@';
      } else
        result.echo[result.bytes++] = byte;
      result.echo[result.bytes++] = '\n';
    }
  } else if (input->canonical && matches(byte, mode->cc[TTY_VERASE])) {
    if (input->used > input->ready) {
      input->used--;
      if (mode->lflag & TTY_ECHO) {
        result.bytes = 3;
        result.echo[0] = '\b';
        result.echo[1] = ' ';
        result.echo[2] = '\b';
      }
    }
  } else if (input->canonical && matches(byte, mode->cc[TTY_VKILL])) {
    input->used = input->ready;
    if (mode->lflag & TTY_ECHO) {
      result.bytes = 1;
      result.echo[0] = '\n';
    }
  } else if (input->canonical && matches(byte, mode->cc[TTY_VEOF])) {
    if (input->used > input->ready) {
      i = (input->head + input->used - 1) % TTY_INPUT_CAPACITY;
      input->data[i] |= INPUT_END;
      input->ready = input->used;
    } else if (input->used < TTY_INPUT_CAPACITY) {
      append(input, INPUT_END | INPUT_EOF);
      input->ready = input->used;
    } else
      drop(input, &result);
  } else {
    line = input->used - input->ready;
    if (input->used == TTY_INPUT_CAPACITY ||
        (input->canonical && byte != '\n' &&
         (line == TTY_LINE_BYTES - 1 || input->used == TTY_INPUT_CAPACITY - 1)))
      drop(input, &result);
    else {
      append(input, (u16) (byte | (input->canonical && byte == '\n' ? INPUT_END : 0)));
      if (!input->canonical || byte == '\n')
        input->ready = input->used;
      if (mode->lflag & TTY_ECHO) {
        result.bytes = 1;
        result.echo[0] = byte;
      }
    }
  }
  *effect = result;
  return 0;
}

isize
ttyinputcopy(Ttyinput *input, volatile u8 *output, u32 capacity, u32 *ended)
{
  u32 i, amount, boundary = 0;
  u16 token;
  if (!ended || !IS_ALIGNED((usize) ended, 4) || capacity > TTY_LINE_BYTES || (capacity && !output))
    return -ERR_INVALID;
  if (!valid(input))
    return -ERR_STATE;
  if (!capacity) {
    *ended = 0;
    return 0;
  }
  if (!input->ready)
    return -ERR_WOULD_BLOCK;
  amount = MIN(capacity, input->ready);
  /* Validate this entire bounded prefix before changing output or position. */
  for (i = 0; i < amount; i++) {
    token = input->data[(input->head + i) % TTY_INPUT_CAPACITY];
    if (input->canonical ? (token & ~(255 | INPUT_END)) && token != (INPUT_END | INPUT_EOF)
                         : token > 255)
      return -ERR_STATE;
    if (token == (INPUT_END | INPUT_EOF)) {
      if (i)
        return -ERR_STATE;
      input->head = (input->head + 1) % TTY_INPUT_CAPACITY;
      input->used--;
      input->ready--;
      *ended = 1;
      return 0;
    }
    if (token & INPUT_END) {
      amount = i + 1;
      boundary = 1;
      break;
    }
  }
  for (i = 0; i < amount; i++)
    output[i] = (u8) input->data[(input->head + i) % TTY_INPUT_CAPACITY];
  input->head = (input->head + amount) % TTY_INPUT_CAPACITY;
  input->used -= amount;
  input->ready -= amount;
  *ended = boundary;
  return (isize) amount;
}

isize
ttyinputread(Ttyinput *input, void *output, u32 capacity, u32 *ended)
{
  if (capacity > TTY_BATCH)
    return -ERR_INVALID;
  return ttyinputcopy(input, output, capacity, ended);
}
