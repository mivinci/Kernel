#ifndef _ABI_TERMINAL_H
#define _ABI_TERMINAL_H
#include <abi/signal.h>
#define TERMINAL_QUERY  1
#define TERMINAL_FG     2
#define TERMINAL_SIGNAL 4
#define TERMINAL_BIND   8
#define TERMINAL_RIGHTS 15
#define TERMINAL_SIGNALS                                                                           \
  ((UINT64_C(1) << (SIGNAL_INT - 1)) | (UINT64_C(1) << (SIGNAL_QUIT - 1)) |                        \
   (UINT64_C(1) << (SIGNAL_TSTP - 1)) | (UINT64_C(1) << (SIGNAL_HUP - 1)) |                        \
   (UINT64_C(1) << (SIGNAL_WINCH - 1)))
#endif
