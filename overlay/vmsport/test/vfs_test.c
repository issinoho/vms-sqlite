/* vfs_test.c - exercise the VMS VFS (os_vms.c) directly.
**
**   1. random reads, writes (partial blocks, gaps past the end), truncates
**      and size checks against an in-memory model; then reopen and compare
**   2. names: xFullPathname of VMS and Unix-style names
**   3. xAccess, xDelete of every version, a temporary file (no name)
**   4. the buffers are 64-bit addresses ($QIO takes them directly)
**   5. P0 growth over 5000 write transactions and 5000 reads (the C RTL
**      leaks on some fcntl() unlock patterns: docs/PHASE0.md)
** Prints "VFS: n failures".
*/
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpidef.h>
#include <lib$routines.h>

static unsigned p0(void)
{
  int item = JPI$_FREP0VA; unsigned v = 0;
  lib$getjpi(&item, 0, 0, &v, 0, 0);
  return v;
}

#define MAXSZ 400000
static unsigned char *model, *buf;
static sqlite3_int64 msize;
static int failures;
static unsigned seed = 12345;

static unsigned rnd(unsigned n) { seed = seed * 1103515245 + 12345; return (seed >> 8) % n; }
static void fail(const char *what, long a, long b)
{
  if (failures++ < 20) printf("VFS FAIL %s (%ld, %ld)\n", what, a, b);
}

static int check_read(sqlite3_file *f, sqlite3_int64 off, int n)
{
  int rc = f->pMethods->xRead(f, buf, n, off), i;
  int expectShort = off + n > msize;
  if ((rc == SQLITE_IOERR_SHORT_READ) != expectShort || (rc && rc != SQLITE_IOERR_SHORT_READ)) {
    fail("read rc", rc, (long) off);
    return 1;
  }
  for (i = 0; i < n; i++) {
    unsigned char want = off + i < msize ? model[off + i] : 0;
    if (buf[i] != want) { fail("read data at", (long) (off + i), buf[i]); return 1; }
  }
  return 0;
}

int main(void)
{
  sqlite3_vfs *vfs;
  sqlite3_file *f;
  int i, rc, out, res;
  sqlite3_int64 sz;
  char full[4200];

  sqlite3_initialize();
  vfs = sqlite3_vfs_find(0);
  printf("VFS default: %s\n", vfs->zName);
  model = calloc(MAXSZ + 70000, 1);
  buf = malloc(MAXSZ + 70000);
  printf("VFS buffers at %p, %p (above 2 GB, not a 32-bit address: %s)\n", (void *) model, (void *) buf,
         (unsigned long) buf >= 0x80000000UL ? "yes" : "no");
  f = malloc(vfs->szOsFile);

  /* 1 */
  vfs->xDelete(vfs, "vfs_test.db", 0);
  rc = vfs->xOpen(vfs, "vfs_test.db", f,
                  SQLITE_OPEN_MAIN_DB | SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE, &out);
  if (rc) { printf("VFS open failed %d\n", rc); return 1; }
  msize = 0;
  for (i = 0; i < 3000; i++) {
    int op = rnd(10);
    if (op < 5) {                               /* write */
      sqlite3_int64 off = rnd(op == 0 ? 8 : 300000);
      int n = 1 + rnd(op == 1 ? 600 : 20000), k;
      if (op == 2) { off -= off % 4096; n = 4096; }   /* a page */
      for (k = 0; k < n; k++) buf[k] = (unsigned char) rnd(256);
      rc = f->pMethods->xWrite(f, buf, n, off);
      if (rc) { fail("write rc", rc, (long) off); continue; }
      memcpy(model + off, buf, n);
      if (off + n > msize) msize = off + n;
    } else if (op < 8) {                        /* read */
      check_read(f, rnd((unsigned) msize + 2000), 1 + rnd(op == 5 ? 700 : 30000));
    } else if (op == 8) {                       /* size */
      f->pMethods->xFileSize(f, &sz);
      if (sz != msize) fail("size", (long) sz, (long) msize);
    } else if (rnd(4) == 0) {                   /* truncate, sometimes */
      sqlite3_int64 t = rnd((unsigned) msize + 1);
      rc = f->pMethods->xTruncate(f, t);
      if (rc) fail("truncate rc", rc, (long) t);
      if (t < msize) memset(model + t, 0, msize - t);
      msize = t;
    } else {
      if (f->pMethods->xSync(f, SQLITE_SYNC_NORMAL)) fail("sync", 0, 0);
    }
  }
  f->pMethods->xClose(f);
  rc = vfs->xOpen(vfs, "vfs_test.db", f, SQLITE_OPEN_MAIN_DB | SQLITE_OPEN_READWRITE, &out);
  f->pMethods->xFileSize(f, &sz);
  if (sz != msize) fail("size after reopen", (long) sz, (long) msize);
  check_read(f, 0, (int) msize);
  f->pMethods->xClose(f);
  printf("VFS random I/O: 3000 operations, final size %ld\n", (long) msize);

  /* 2 */
  vfs->xFullPathname(vfs, "vfs_test.db", sizeof full, full);
  printf("VFS full name of vfs_test.db: %s\n", full);
  vfs->xFullPathname(vfs, "./vfs_test.db", sizeof full, full);
  printf("VFS full name of ./vfs_test.db: %s\n", full);
  vfs->xFullPathname(vfs, "sys$scratch:x.db", sizeof full, full);
  printf("VFS full name of sys$scratch:x.db: %s\n", full);
  rc = vfs->xFullPathname(vfs, "/sys$scratch/y.db", sizeof full, full);
  printf("VFS full name of /sys$scratch/y.db: %s (rc %d)\n", full, rc);
  {
    static const char *names[] = { "../RUN/vfs_test.db", "../run/vfs_test.db", "sub/x.db", "a.b.c.db" };
    int k;
    for (k = 0; k < 4; k++) {
      full[0] = 0;
      rc = vfs->xFullPathname(vfs, names[k], sizeof full, full);
      printf("VFS full name of %s: %s (rc %d)\n", names[k], full, rc);
    }
  }

  /* 3 */
  vfs->xAccess(vfs, "vfs_test.db", SQLITE_ACCESS_EXISTS, &res);
  if (!res) fail("access exists", 0, 0);
  vfs->xAccess(vfs, "no_such.db", SQLITE_ACCESS_EXISTS, &res);
  if (res) fail("access missing", 0, 0);
  for (i = 0; i < 3; i++) {                     /* three versions of a journal */
    FILE *fp = fopen("vfs_test.db-journal", "w", "shr=get");
    fputs("x", fp); fclose(fp);
  }
  rc = vfs->xDelete(vfs, "vfs_test.db-journal", 0);
  vfs->xAccess(vfs, "vfs_test.db-journal", SQLITE_ACCESS_EXISTS, &res);
  if (rc || res) fail("delete all versions", rc, res);
  rc = vfs->xDelete(vfs, "vfs_test.db-journal", 0);
  if (rc != SQLITE_IOERR_DELETE_NOENT) fail("delete missing", rc, 0);
  rc = vfs->xOpen(vfs, 0, f, SQLITE_OPEN_TEMP_DB | SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE |
                  SQLITE_OPEN_DELETEONCLOSE, &out);
  if (rc) fail("temp open", rc, 0);
  else {
    memset(buf, 'T', 10000);
    msize = 10000; memset(model, 'T', 10000);
    f->pMethods->xWrite(f, buf, 10000, 0);
    check_read(f, 0, 10000);
    f->pMethods->xClose(f);
  }
  vfs->xDelete(vfs, "vfs_test.db", 0);

  /* 5: P0 growth per kind of transaction */
  {
    static const struct { const char *name, *setup; int write; } ph[] = {
      { "read transactions", "", 0 },
      { "writes, journal_mode=DELETE", "pragma journal_mode=delete", 1 },
      { "writes, journal_mode=TRUNCATE", "pragma journal_mode=truncate", 1 },
      { "writes, locking_mode=EXCLUSIVE", "pragma locking_mode=exclusive; pragma journal_mode=truncate", 1 },
    };
    int ip;
    for (ip = 0; ip < 4; ip++) {
      sqlite3 *db;
      sqlite3_stmt *ins, *sel;
      unsigned before = 0;
      int k;
      vfs->xDelete(vfs, "leak_test.db", 0);
      sqlite3_open("leak_test.db", &db);
      sqlite3_exec(db, ph[ip].setup, 0, 0, 0);
      sqlite3_exec(db, "create table t(a integer)", 0, 0, 0);
      sqlite3_prepare_v2(db, "insert into t values(?)", -1, &ins, 0);
      sqlite3_prepare_v2(db, "select count(*) from t", -1, &sel, 0);
      for (k = 0; k < 5500; k++) {
        if (k == 500) before = p0();            /* after the caches have grown */
        if (ph[ip].write) {
          sqlite3_exec(db, "begin immediate", 0, 0, 0);
          sqlite3_bind_int(ins, 1, k); sqlite3_step(ins); sqlite3_reset(ins);
          sqlite3_exec(db, "commit", 0, 0, 0);
        } else {
          sqlite3_step(sel); sqlite3_reset(sel);
        }
      }
      printf("VFS P0 growth, 5000 %-34s %8u bytes (%.0f per transaction)\n", ph[ip].name,
             p0() - before, (p0() - before) / 5000.0);
      sqlite3_finalize(ins); sqlite3_finalize(sel);
      sqlite3_close(db);
      vfs->xDelete(vfs, "leak_test.db", 0);
    }
    vfs->xDelete(vfs, "leak_test.db-lock", 0);
    vfs->xDelete(vfs, "leak_test.db-journal", 0);
  }
  printf("VFS: %d failures\n", failures);
  return failures != 0;
}
