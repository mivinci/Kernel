#ifndef _PROTO_FILE_H
#define _PROTO_FILE_H
#include <types.h>
#define FS_CONN_MAGIC       UINT64_C(0x4b434f4e4e303031)
#define FS_CONN_SLOTS       8
#define FS_SLOT_BYTES       4096
#define FS_CONN_BYTES       (9 * FS_SLOT_BYTES)
#define FS_DESC_OFFSET      64
#define FS_DESC_BYTES       128
#define FS_DATA_OFFSET      FS_SLOT_BYTES
#define FS_CONN_COUNT       128
#define FS_SLOT_FREE        0
#define FS_SLOT_SUBMITTED   1
#define FS_SLOT_DONE        2
#define FS_SLOT_QUARANTINED 3
#define FS_HELLO            1
#define FS_OPEN             2
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
#define FS_PATH             10
#define FS_POLL_QUERY       11
#define FS_POLL_WAIT        12
#define FS_POLL_ROWS        128
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
typedef struct Filehead    Filehead;
typedef struct Fileslot    Fileslot;
typedef struct Fileinherit Fileinherit;
typedef struct Filepoll    Filepoll;
struct Filehead
{
  u64 magic, bytes, connid, serverimage, slots, slotbytes, reserved[2];
};
struct Fileslot
{
  u64 seq, state, payload_bytes, object, span, reserved[3], args[8];
};
/* One descriptor row of a FORK/EXEC preparation snapshot. */
struct Fileinherit
{
  u64 fd, flags, backend_pid, backend_image, session, reference_id;
};
struct Filepoll
{
  u64 session, fd_generation, events, revents;
};
STATIC_ASSERT(sizeof(Filepoll) == 32 && FS_POLL_ROWS * sizeof(Filepoll) == FS_SLOT_BYTES,
              file_poll_rows_fit_page);
STATIC_ASSERT(offsetof(Filepoll, revents) == 24, file_poll_output_offset);
STATIC_ASSERT(sizeof(Fileinherit) == 48, file_inherit_record);
STATIC_ASSERT(sizeof(Filehead) == 64, file_connection_header);
STATIC_ASSERT(sizeof(Fileslot) == FS_DESC_BYTES, file_slot_descriptor);
STATIC_ASSERT(offsetof(Fileslot, span) == 32, file_slot_span);
STATIC_ASSERT(offsetof(Fileslot, args) == 64, file_slot_arguments);
STATIC_ASSERT(FS_DESC_OFFSET + FS_CONN_SLOTS * FS_DESC_BYTES <= FS_DATA_OFFSET, file_control_page);
#endif
