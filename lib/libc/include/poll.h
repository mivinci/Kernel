#ifndef _POLL_H
#define _POLL_H
typedef unsigned long nfds_t;

struct pollfd
{
  int   fd;
  short events, revents;
};

#define POLLIN   1
#define POLLPRI  2
#define POLLOUT  4
#define POLLERR  8
#define POLLHUP  16
#define POLLNVAL 32

int poll(struct pollfd *fds, nfds_t count, int timeout);
#endif
