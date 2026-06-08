#include <chr.h>
#include <types.h>

static ChrOps *console_ops;

void chr_set_console(ChrOps *ops) {
  console_ops = ops;
}

int chr_read(void) {
  return console_ops ? console_ops->read() : -1;
}

void chr_write(int c) {
  if (console_ops) console_ops->write(c);
}

int chr_has_data(void) {
  return console_ops ? console_ops->has_data() : 0;
}
