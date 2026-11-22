#ifndef _LIBSYS_FILE_H
#define _LIBSYS_FILE_H
#include <proto/file.h>
#include <abi/base.h>
#define SYSFILE_MAIN    0
#define SYSFILE_SIGNAL1 1
#define SYSFILE_SIGNAL2 2
#define SYSFILE_CONTROL 3
#define SYSFILE_CLEANUP 4
typedef struct Filelease    Filelease;
typedef struct Filepages    Filepages;
typedef struct Fileinflight Fileinflight;
struct Filelease
{
  u64 seq;
  u32 slot, span;
};
struct Filepages
{
  u64       sequence;
  Filelease pages[FS_CONN_SLOTS];
};
struct Fileinflight
{
  Handle requests[FS_CONN_SLOTS];
};
STATIC_ASSERT(sizeof(Filelease) == 16, file_page_lease);
STATIC_ASSERT(sizeof(Filepages) <= PAGE_SIZE, file_page_bookkeeping);
/* Private fixed-header snapshots. Caller has already selected the exact
   backend/source connection, owns valid mapped C storage, and supplies the
   actual granted extent and private identities. No shared field authorizes a
   memory range. Output storage must not overlap the region; failure keeps it.
   head checks its fixed fields, not the whole control-page padding. Request
   checks common shape only; caller still checks opcode/object/unused args and
   privately registers the entire span before reading payload. */
isize sysfilehead(const volatile void *region, u64 bytes, u64 connid, u64 serverimage,
                  Filehead *out);
isize sysfilerequest(const volatile void *region, u64 bytes, u32 slot, u64 seq, Fileslot *out);
/* Validate a privately captured READ/WRITE/CLOSE/DUPREF descriptor before
   touching payload. File mode, offset and reference authority are separate. */
isize sysfileargs(u64 opcode, const Fileslot *slot);
/* Snapshot the rows from one admitted QUERY/WAIT request into private
   storage. Capacity is given in rows. Event semantics are checked by the
   backend before using or writing any rows. */
isize sysfilepollrows(const Fileinflight *active, Handle request, const volatile void *region,
                      u64 bytes, u32 index, const Fileslot *slot, Filepoll *out, u32 capacity);
/* Encode an already captured client group into its reserved one-page lease.
   The caller owns the lease and the disjoint private input. */
isize sysfilepollstage(volatile void *region, u64 bytes, Filelease lease, const Filepoll *rows,
                       u32 count, u64 deadline, u64 generation);
/* Decode a successful ordinary reply after native FINISH. Validate the
   echoed rows against the original private group before updating revents. */
isize sysfilepollresult(const volatile void *region, u64 bytes, Filelease lease, const u64 words[4],
                        Filepoll *rows, u32 count);
/* Zero-initialized private ledger for one exact service connection. The
   serialized service loop verifies source and connid before admission.
   Keep the full REQUEST reserved through defer, cancellation and downstream
   access. Finish only after access has ended and the original reply or
   other actual request completion has been established. */
isize sysfileaccept(Fileinflight *active, const volatile void *region, u64 bytes, u64 slot, u64 seq,
                    Handle request, Fileslot *out);
isize sysfilefinish(Fileinflight *active, Handle request);
/* Per-connection client page ledger, separate from writable shared state.
   Callers exclude caught signals for each short operation. Init is fresh
   unowned storage only. No RPC, File reference or completion proof lives here.
   Leaving an operation/Rctx does not automatically release its pages. */
void  sysfilepagesinit(Filepages *pages);
isize sysfilepagescheck(const Filepages *pages);
isize sysfilereserve(Filepages *pages, u32 context, u32 span, Filelease *out);
/* Only after result/data/resource ownership is saved and actual access has
   ended; shared DONE alone is insufficient. Late/stale tags release nothing. */
isize sysfilerelease(Filepages *pages, Filelease expected);
#endif
