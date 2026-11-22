#ifndef _LIBSYS_FILEIMAGE_H
#define _LIBSYS_FILEIMAGE_H
#include "contextipc.h"
#include "fd.h"
#include "filecreate.h"

typedef struct Fileimage    Fileimage;
typedef struct Fileioresult Fileioresult;
typedef struct Pollfd       Pollfd;
typedef struct Pollfile     Pollfile;
struct Fileimage
{
  Connwork connections[SYSCONN_COUNT];
  Fdtable  fds;
  Filepool files;
  Rbank    contexts;
};
struct Fileioresult
{
  isize native, business;
  u64   count;
};
struct Pollfd
{
  int fd;
  u16 events, revents;
};
struct Pollfile
{
  Filepoll row;
  Conntag  connection;
  u64      file;
  int      fd;
};

/* Caller excludes caught signals and supplies disjoint private arrays.
   Each live item holds a File pin until release; invalid and negative fds
   have local results and need no transport. Failure returns all acquired pins. */
isize sysfilepollcapture(Fileimage *state, const Pollfd *fds, Pollfile *items, u32 count);
isize sysfilepollrelease(Fileimage *state, Pollfile *items, u32 count);
/* Recheck the saved descriptor identity without following a replacement. */
isize sysfilepollcurrent(const Fileimage *state, const Pollfile *item);
/* Build one wire group from the captured entries. Indices map the reply back
   to the original array, including repeated fds. Output capacity is in rows. */
isize sysfilepollgroup(const Pollfile *items, u32 count, Conntag connection, Filepoll *rows,
                       u32 *indices, u32 capacity);
/* Hand off the original group to the image's completion worker. */
isize sysfilepollabandon(Fileimage *state, Opkey *key);
/* Submit while caught signals are excluded. A rejected call remains PREPARED. */
isize sysfilepollsubmit(Connwork *work, Conncache *cache, const Opkey *key);
/* Nonblocking collection. FINISH precedes payload validation and ACK.
   Keep the same private rows and result across retries. Zero returns the
   group's pin exactly once, clears the key, and publishes its outcome. */
isize sysfilepollcollect(Connwork *work, Conncache *cache, Opkey *key, Filepoll *rows, u32 count,
                         Fileioresult *result, u64 *pin);

/* One shared file state per image, initialized on demand. Inherited image
   repair must be wired before these records can be used by a fork child. */
isize      sysfileimage(Fileimage **out);
Conncache *sysfileimagecache(Fileimage *image);
/* Resolve the fixed Boot FS endpoint and continue its initial HELLO. The
   caller provides a reserved mapping address for the cold path. A failed
   map with no alias permits retry at another address. A ready connection
   ignores the proposed address; all callers use the same table and backend. */
isize sysfileimagefs(u32 layer, u64 address, Connwork **out);
/* Drive PIPE on the established FS connection. The caller retains the
   continuation; failed copyout hands both File references to the image worker. */
isize sysfileimagepipe(u32 layer, Connwork *work, Pipecreate *pipe, Rkey *active, int *fds,
                       isize *business);
/* Stage one READ/WRITE chunk on the exact live File. The caller excludes
   caught signals through Rctx registration and owns the resulting Opkey. */
isize sysfileiostage(Fileimage *state, u32 layer, int fd, u64 opcode, const void *input, u64 bytes,
                     Connwork **out, Opkey *key);
/* Submit once and wait for the original RPC's result and FINISH proof.
   Caller holds caught signals blocked except while waiting with mask.
   Interruption or timeout leaves the original key and its obligation live.
   On interruption, restart receives the handler's captured SA_RESTART bit. */
isize sysfileiowait(Fileimage *state, u32 layer, const Opkey *key, u64 mask, u32 *restart);
/* After interruption, check the original result before the cancellation
   deadline. SYSOP_ABANDONED consumes key and schedules background cleanup. */
isize sysfileioresume(Fileimage *state, Opkey *key);
/* After a restartable signal, replace a proved zero-effect operation with a
   new draft using its File pin and original snapshot. Caught signals stay
   blocked; bytes is the original length. On failure after retirement, the
   key's sequence is zero and there is no remaining foreground operation. */
isize sysfileiorestart(Fileimage *state, Opkey *key, u64 bytes);
/* Collect a submitted chunk without blocking. Keep signals masked and use
   the same private result storage and original byte limit across retries.
   Zero means the operation was released; inspect result for its outcome. */
isize sysfileiofinish(Fileimage *state, const Opkey *key, void *output, u64 bytes,
                      Fileioresult *result);
isize sysfileimageio(u32 layer, int fd, u64 opcode, const void *input, void *output, u64 bytes,
                     Fileioresult *result);
isize sysfileimageposio(u32 layer, int fd, u64 opcode, const void *input, void *output, u64 bytes,
                        int64_t offset, Fileioresult *result);
isize sysfileimageseek(u32 layer, int fd, int64_t offset, int whence, Fileioresult *result);
/* Duplicate a live descriptor in the current image without a service RPC. */
isize sysfileimagedup(int fd);
isize sysfileimagedup2(int fd, int target);
isize sysfileimagedupfrom(int fd, int minimum);
/* Read or replace the flags on one live descriptor without changing its File. */
isize sysfileimageflags(int fd, u32 set, u64 flags);
/* ERR_INTERRUPTED means the fd was detached and cleanup still owns the File. */
isize sysfileimageclose(int fd);
#endif
