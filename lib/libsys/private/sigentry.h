#ifndef _LIBSYS_SIGENTRY_H
#define _LIBSYS_SIGENTRY_H
#include <abi/signal.h>

struct Rbank;
/* Publish an initialized bank in private storage for the life of this image. */
isize sysctxsigbind(struct Rbank *bank);
isize sysctxsigentry(const Sighead *frame);
#endif
