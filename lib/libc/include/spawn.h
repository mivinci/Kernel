#ifndef _SPAWN_H
#define _SPAWN_H
#include <sys/types.h>

struct Spawnaction;
typedef struct Spawnfiles posix_spawn_file_actions_t;
struct Spawnfiles
{
  struct Spawnaction *__first, *__last;
  unsigned int        __count;
};

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *);
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *, int, const char *, int, mode_t);
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *, int);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *, int, int);
#endif
