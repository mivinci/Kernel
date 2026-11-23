#ifndef _PROTO_FILE_H
#define _PROTO_FILE_H
#include <proto/pipe.h>
#include <proto/slot.h>
#define FS_CONN_MAGIC       PROTO_CONN_MAGIC
#define FS_CONN_SLOTS       PROTO_CONN_SLOTS
#define FS_SLOT_BYTES       PROTO_SLOT_BYTES
#define FS_CONN_BYTES       PROTO_CONN_BYTES
#define FS_DESC_OFFSET      PROTO_DESC_OFFSET
#define FS_DESC_BYTES       PROTO_DESC_BYTES
#define FS_DATA_OFFSET      PROTO_DATA_OFFSET
#define FS_CONN_COUNT       128
#define FS_SLOT_FREE        PROTO_SLOT_FREE
#define FS_SLOT_SUBMITTED   PROTO_SLOT_SUBMITTED
#define FS_SLOT_DONE        PROTO_SLOT_DONE
#define FS_SLOT_QUARANTINED PROTO_SLOT_QUARANTINED
#define FS_HELLO            1
#define FS_OPEN             2
#define FS_OPEN_RDONLY      0
#define FS_OPEN_WRONLY      1
#define FS_OPEN_RDWR        2
#define FS_OPEN_ACCMODE     3
#define FS_OPEN_CREAT       0x40
#define FS_OPEN_EXCL        0x80
#define FS_OPEN_TRUNC       0x200
#define FS_OPEN_APPEND      0x400
#define FS_OPEN_NONBLOCK    0x800
#define FS_OPEN_CLOEXEC     0x80000
#define FS_ATTACH           3
#define FS_READ             4
#define FS_WRITE            5
#define FS_CLOSE            6
#define FS_DUPREF           7
#define FS_SEEK             8
#define FS_SEEK_SET         0
#define FS_SEEK_CUR         1
#define FS_SEEK_END         2
#define FS_STAT             9
#define FS_STAT_BYTES       128
#define FS_PATH             10
#define FS_POLL_QUERY       11
#define FS_POLL_WAIT        12
#define FS_POLL_ROWS        128
#define FS_POLL_IN          1
#define FS_POLL_PRI         2
#define FS_POLL_OUT         4
#define FS_POLL_ERR         8
#define FS_POLL_HUP         16
#define FS_POLL_NVAL        32
#define FS_FORK_SNAPSHOT    16
#define FS_FORK_PREPARE     17
#define FS_EXEC_PREPARE     18
#define FS_EXEC_CLAIM       19
#define FS_ABORT            20
#define FS_SYNC             21
#define FS_LOAD             22
#define FS_TTYCTL           23
#define FS_PIPE             24
#define FS_IO_POSITIONAL    1
#define FS_IO_APPEND        2
#define FS_PATH_LOOKUP      1
#define FS_PATH_CREATE      2
#define FS_PATH_MKDIR       3
#define FS_PATH_UNLINK      4
#define FS_PATH_RMDIR       5
#define FS_PATH_RENAME      6
#define FS_PATH_LINK        7
#define FS_PATH_SYMLINK     8
#define FS_PATH_READLINK    9
#define FS_PATH_CHMOD       10
#define FS_PATH_TRUNCATE    11
#define FS_PATH_UTIMENS     12
#define FS_PATH_GETCWD      13
#define FS_PATH_CHDIR       14
typedef Protohead          Filehead;
typedef Protoslot          Fileslot;
typedef struct Fileinherit Fileinherit;
typedef struct Filepoll    Filepoll;
typedef struct Filestat    Filestat;
/* One descriptor row of a FORK/EXEC preparation snapshot. */
struct Fileinherit
{
  u64 fd, flags, backend_pid, backend_image, session, reference_id;
};
struct Filepoll
{
  u64 session, fd_generation, events, revents;
};
/* Wire result for STAT. This type is separate from the public struct stat. */
struct Filestat
{
  u64     dev, ino, mode, nlink, uid, gid, size, blksize, blocks;
  int64_t atime_sec;
  u64     atime_ns;
  int64_t mtime_sec;
  u64     mtime_ns;
  int64_t ctime_sec;
  u64     ctime_ns, rdev;
};
STATIC_ASSERT(sizeof(Filestat) == FS_STAT_BYTES, file_stat_size);
STATIC_ASSERT(offsetof(Filestat, atime_sec) == 72, file_stat_atime);
STATIC_ASSERT(offsetof(Filestat, rdev) == 120, file_stat_rdev);
static __inline__ int
filestatvalid(const Filestat *record)
{
  u64 kind = record->mode & 0170000;
  return (!record->rdev || kind == 0020000 || kind == 0060000) &&
         record->atime_ns < UINT64_C(1000000000) && record->mtime_ns < UINT64_C(1000000000) &&
         record->ctime_ns < UINT64_C(1000000000);
}
STATIC_ASSERT(sizeof(Filepoll) == 32 && FS_POLL_ROWS * sizeof(Filepoll) == FS_SLOT_BYTES,
              file_poll_rows_fit_page);
STATIC_ASSERT(offsetof(Filepoll, revents) == 24, file_poll_output_offset);
STATIC_ASSERT(sizeof(Fileinherit) == 48, file_inherit_record);
STATIC_ASSERT(FS_PIPE_BUF <= FS_SLOT_BYTES, pipe_atomic_write_fits_file_slot);
#endif
