/* p_leak.c - does SQLite's lock/unlock cycle leak C RTL (P0) memory?
   vms-php #53: unlocking a range wider than the lock held costs ~440 bytes
   of P0 each time.  SQLite's transaction (unixLock/posixUnlock) takes a read
   lock on the SHARED range, write locks on RESERVED and PENDING, then unlocks
   PENDING+RESERVED (2 bytes) and finally the whole file (l_start 0, l_len 0).
   Variant 1: that cycle as SQLite does it.  Variant 2: the same, but the last
   step unlocks exactly the SHARED range.  Variant 3: no conversions and no
   merged ranges: each lock is released with exactly its own range before the
   next is taken.  Variant 4: only 1-byte locks, each released exactly.
   Variant 5: a read lock converted to a write lock on the same range, then
   released exactly.  Prints P0 growth per cycle. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <jpidef.h>
#include <lib$routines.h>

#define PENDING 0x40000000L

static int lk(int fd, int type, long start, long len)
{
  struct flock l;
  memset(&l, 0, sizeof l);
  l.l_type = type; l.l_whence = SEEK_SET; l.l_start = start; l.l_len = len;
  return fcntl(fd, F_SETLK, &l);
}

static unsigned p0(void)
{
  int item = JPI$_FREP0VA; unsigned v = 0;
  lib$getjpi(&item, 0, 0, &v, 0, 0);
  return v;
}

int main(void)
{
  int fd = open("leak_test.db", O_RDWR | O_CREAT, 0644), v, i, n = 5000, bad = 0;
  for (v = 1; v <= 5; v++) {
    unsigned before = p0();
    for (i = 0; i < n; i++) {
      if (v <= 2) {
        bad |= lk(fd, F_RDLCK, PENDING + 2, 510);   /* SHARED */
        bad |= lk(fd, F_WRLCK, PENDING + 1, 1);     /* RESERVED */
        bad |= lk(fd, F_WRLCK, PENDING, 1);         /* PENDING */
        bad |= lk(fd, F_WRLCK, PENDING + 2, 510);   /* EXCLUSIVE */
        bad |= lk(fd, F_RDLCK, PENDING + 2, 510);   /* back to SHARED */
        bad |= lk(fd, F_UNLCK, PENDING, 2);
        if (v == 1) bad |= lk(fd, F_UNLCK, 0, 0);   /* NO_LOCK, as posixUnlock */
        else bad |= lk(fd, F_UNLCK, PENDING + 2, 510);
      } else if (v == 3) {
        bad |= lk(fd, F_RDLCK, PENDING + 2, 510); bad |= lk(fd, F_UNLCK, PENDING + 2, 510);
        bad |= lk(fd, F_WRLCK, PENDING + 1, 1);   bad |= lk(fd, F_UNLCK, PENDING + 1, 1);
        bad |= lk(fd, F_WRLCK, PENDING + 2, 510); bad |= lk(fd, F_UNLCK, PENDING + 2, 510);
      } else if (v == 4) {
        bad |= lk(fd, F_WRLCK, PENDING, 1);     bad |= lk(fd, F_UNLCK, PENDING, 1);
        bad |= lk(fd, F_RDLCK, PENDING + 1, 1); bad |= lk(fd, F_UNLCK, PENDING + 1, 1);
      } else {
        bad |= lk(fd, F_RDLCK, PENDING + 2, 510); bad |= lk(fd, F_WRLCK, PENDING + 2, 510);
        bad |= lk(fd, F_UNLCK, PENDING + 2, 510);
      }
    }
    printf("LEAK variant %d (%s): P0 +%u bytes over %d cycles = %.1f per cycle%s\n", v,
           v == 1 ? "SQLite, unlock whole file" : v == 2 ? "SQLite, unlock SHARED range" : v == 3 ? "exact pairs, 510 bytes" : v == 4 ? "exact pairs, 1 byte" : "convert read->write, exact unlock", p0() - before, n,
           (p0() - before) / (double) n, bad ? " (a lock call failed)" : "");
  }
  close(fd);
  return 0;
}
