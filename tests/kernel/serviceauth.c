/* Native bootstrap authority exercise; never publishes a fake service. */
#include <sys/boot.h>
#include <abi/resource.h>
#include "private/start.h"
#include "private/syscall.h"

#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c))                                                                                      \
      return n;                                                                                    \
  } while (0)

int
main(int argc, char **argv)
{
  const Bootview *boot = sysboot();
  Handle          creator, listener = 0, view, pid, control, installed;
  Handle          self, parent;
  u64             image, cursor = 0, written, next, i, creates = 0, found = 0;
  Queryrec        records[4], own, child;
  Createarg       args;
  isize           error;
  (void) argv;
  REQUIRE(argc == 1 && !__sys_getid(&self, &image, &parent) && self == 1 && !parent, 100);
  creator = bootresource(boot, BOOT_CREATE);
  REQUIRE(creator > 0, 101);
  do {
    REQUIRE(!__sys_query(0, QUERY_RESOURCE, cursor, records, 4, &written, &next), 102);
    for (i = 0; i < written; i++) {
      if (records[i].data[1] == RESOURCE_CREATE) {
        creates++;
        REQUIRE((records[i].data[2] & (CREATE_RIGHTS | (1UL << 63))) ==
                    (CREATE_RIGHTS | (1UL << 63)),
                103);
        if ((Handle) records[i].data[0] == creator)
          found++;
      }
      if (records[i].data[1] == RESOURCE_CALL && records[i].data[6] == 1 &&
          records[i].data[7] == image && (records[i].data[2] & (1UL << 63))) {
        REQUIRE(!listener, 104);
        listener = (Handle) records[i].data[0];
      }
    }
    cursor = next;
  } while (cursor);
  REQUIRE(creates == 16 && found == 1 && listener, 105);
  REQUIRE(!__sys_query(0, QUERY_ACCT, 0, &own, 1, &written, &next) && written == 1 && !next, 106);
  args.memlimit = 64;
  args.exec_limit = 128;
  args.resources = 8;
  args.pending = 1;
  args.ruid = args.euid = args.rgid = args.egid = 0;
  args.reserved = 0;
  REQUIRE(!__sys_create(creator, &args, &view, &pid, &control) && view > 0 && pid > 1 &&
              control > 0,
          107);
  REQUIRE(!__sys_query(control, QUERY_ACCT, 0, &child, 1, &written, &next) && written == 1 &&
              !next && child.data[0] != own.data[0] && child.data[1] == 64 && child.data[2] == 128,
          108);
  REQUIRE(!__sys_grant(view, listener, 1, 0, 0, 0, &installed) && installed > 0, 109);
  REQUIRE(!__sys_query(view, QUERY_RESOURCE, 0, records, 4, &written, &next), 110);
  found = 0;
  for (i = 0; i < written; i++)
    if ((Handle) records[i].data[0] == installed) {
      REQUIRE(records[i].data[1] == RESOURCE_CALL && records[i].data[2] == 1 &&
                  records[i].data[6] == 1 && records[i].data[7] == image,
              111);
      found++;
    }
  REQUIRE(found == 1, 112);
  REQUIRE(!__sys_destroy(view) && !__sys_free(control), 113);
  error = __sys_wait(-1, WAIT_WNOHANG, &pid, &image, &cursor, &next);
  REQUIRE(error == -ERR_CHILD, 114);
  return 42;
}
