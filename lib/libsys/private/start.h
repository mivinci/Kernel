#ifndef _LIBSYS_START_H
#define _LIBSYS_START_H
#include <sys/boot.h>
typedef isize (*Bootfdinit)(const Bootview *, void *);
/* The adapter installs local state before the Boot view becomes visible. */
isize sysstartwithfds(const Boot *boot, u64 available, Bootfdinit install, void *context,
                      const Bootview **out);
/* Runtime adapter: bind only fully checked, immutable startup metadata. */
isize           sysstart(const Boot *boot, u64 available, const Bootview **out);
isize           sysstartargs(const Boot *boot, int *argc, char ***argv, char ***envp);
isize           sysstartargswithfds(const Boot *boot, int *argc, char ***argv, char ***envp,
                                    Bootfdinit install);
const Bootview *sysboot(void);
#endif
