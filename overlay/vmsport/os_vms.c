/*
** os_vms.c - the SQLite VFS for OpenVMS ("vms").
**
** Built with SQLITE_OS_OTHER=1: sqlite3_os_init() below registers this VFS
** as the default.  Why SQLite's unix VFS can't be used on VMS, and the design
** of this one, are in docs/DECISIONS.md (D5) and docs/PHASE0.md:
**
**  - The C RTL's file descriptors are not coherent between processes (each
**    keeps its own copy of blocks it wrote; fstat lags; closing a writable
**    descriptor writes its own idea of the end of file into the header).
**    So file I/O here bypasses the C RTL: RMS opens the file with UFO and
**    hands over the channel, data moves with $QIO virtual-block reads and
**    writes (coherent through XFC), and the end of file is the file
**    header's record attributes, read and written through the ACP.
**
**  - Locks: SQLite's lock bytes (PENDING, RESERVED, the SHARED range) are
**    fcntl() locks, as in the unix VFS, but on a companion file
**    "<database>-lock" that is never read or written, and every lock is
**    released with exactly the range that was locked: the C RTL leaks P0
**    memory on in-place conversions and wider unlocks.  As in the unix VFS,
**    POSIX locks belong to the process, so one record per lock file per
**    process (VmsLockFile) holds them for all its connections.
**
**  - A $QIO costs ~0.3 ms to read and, as XFC writes through, ~1 ms to
**    write.  So reads and writes go through a cache per file of VMS_NWIN
**    windows of 32 KB, each with a valid and a dirty bit per block: a missing
**    window is read with one $QIO, and the dirty blocks go out as one $QIO
**    per run when anyone else could look: sync, truncate, close, before a
**    write lock is released, and when a dirty window is evicted.
**    On Unix a process's writes outlive it in the OS cache, and SQLite relies
**    on that (synchronous=OFF, a crash mid-transaction): an exit handler
**    writes every buffer and end of file when the image exits.  STOP/ID runs
**    no exit handlers; it is then a power failure, which SQLite's sync points
**    cover unless synchronous=OFF.
**
**  - Coherence of that cache: with a rollback journal no other process can
**    change the database while this one holds a lock, and across
**    transactions the cache is kept while the database's change counter
**    (bytes 24-27, which SQLite increments on every commit) is unchanged.
**    WAL would need another rule: there the cache must go.
**
**  - An ACP call costs ~0.6 ms (p_cost).  While this connection holds a lock
**    on the database no other process can change it, so the end of file is
**    read from the header once per lock (and not at all when block 1 shows
**    the same change counter and size, sameFile), then known (eofLocked).
**    The counter alone is not enough: a hot journal's rollback restores it
**    while truncating the file.  A writer killed in a cache spill may have
**    extended the file without a commit: the end of file is read again
**    before this connection writes (RESERVED, or EXCLUSIVE to roll back its
**    journal).
**
**  - RMS and ACP control blocks have 32-bit address fields; they live in a
**    per-file block allocated in 32-bit memory (VmsIo).  $QIOW takes 64-bit
**    buffer addresses, so pages move to and from SQLite's buffers directly.
**
** Not yet: shared memory for WAL (the methods are version 1, so WAL needs
** locking_mode=EXCLUSIVE), and loadable extensions.
*/
#include "sqlite3.h"

#include <atrdef.h>
#include <descrip.h>
#include <efndef.h>
#include <fatdef.h>
#include <fibdef.h>
#include <iodef.h>
#include <rms.h>
#include <rmsdef.h>
#include <ssdef.h>
#include <starlet.h>
#include <unixlib.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define VMS_MAXPATH   4095          /* NAML$C_MAXRSS */
#define VMS_BLOCK     512
#define VMS_XFER      32768         /* bytes per $QIO */
#define VMS_CHUNK     64            /* default extension, blocks */
#define VMS_NWIN      16            /* cache: windows of VMS_XFER bytes per file */
#define WIN_BLOCKS    (VMS_XFER / VMS_BLOCK)        /* 64: one bit each */

/* The lock bytes in "<database>-lock".  The unix VFS uses adjacent bytes in
** the database itself (PENDING 0x40000000, RESERVED +1, SHARED +2..+511); the
** lock file is ours alone, so here they are spaced out: the C RTL leaks P0
** memory when a writer holds adjacent ranges and releases one of them
** (898 bytes per write transaction, vfs_test).  Every VMS VFS must use this
** layout. */
#define PENDING_BYTE  0L
#define RESERVED_BYTE 2L
#define SHARED_FIRST  4L
#define SHARED_SIZE   510

#define NO_LOCK        SQLITE_LOCK_NONE
#define SHARED_LOCK    SQLITE_LOCK_SHARED
#define RESERVED_LOCK  SQLITE_LOCK_RESERVED
#define PENDING_LOCK   SQLITE_LOCK_PENDING
#define EXCLUSIVE_LOCK SQLITE_LOCK_EXCLUSIVE

typedef sqlite3_int64 i64;

/* RMS and ACP blocks: everything here is referenced by 32-bit fields. */
typedef struct VmsIo {
  struct FAB fab;
  struct NAML naml;
  struct fibdef fib;
  struct dsc$descriptor fibd;
  struct atrdef atr[2];
  FAT fat;
  unsigned short iosb[4];
  char name[VMS_MAXPATH + 1];       /* file name given to RMS */
  char esa[VMS_MAXPATH + 1];        /* expanded name */
  char rsa[VMS_MAXPATH + 1];        /* resultant name */
  char dna[VMS_MAXPATH + 1];        /* default name (temporary files) */
  char edge[VMS_BLOCK];             /* read-modify-write of partial blocks */
} VmsIo;

/* One per lock file per process: POSIX locks belong to the process. */
typedef struct VmsLockFile VmsLockFile;
struct VmsLockFile {
  VmsLockFile *next;
  dev_t dev;
  ino_t ino;
  int fd;                           /* C RTL descriptor, for fcntl() only */
  int nRef;                         /* VmsFiles using this record */
  int nShared;                      /* connections holding SHARED or more */
  int eLock;                        /* strongest lock in this process */
  int shared;                       /* SHARED range held: 0, F_RDLCK, F_WRLCK */
  int pending;                      /* PENDING byte held (write) */
  int reserved;                     /* RESERVED byte held (write) */
};

typedef struct VmsFile VmsFile;
struct VmsFile {
  sqlite3_file base;
  VmsFile *pNext, *pPrev;           /* open files, for the exit handler */
  VmsIo *io;                        /* 32-bit block */
  unsigned short chan;
  unsigned short fid[3];
  int isReadonly;
  int eLock;                        /* this connection's lock */
  VmsLockFile *lf;                  /* set at the first xLock */
  i64 eof;                          /* end of file, including buffered writes */
  i64 dEof;                         /* end of what is on the disk */
  int eofDirty;                     /* eof not yet written to the header */
  int eofLocked;                    /* eof current: we hold a lock (database) */
  char *wins;                       /* the cache: VMS_NWIN windows, then scratch */
  char *scratch;                    /*   one window's worth, for filling */
  i64 winOff[VMS_NWIN];             /*   window offsets, -1 empty */
  unsigned long long winValid[VMS_NWIN], winDirty[VMS_NWIN];   /* a bit per block */
  unsigned winUse[VMS_NWIN], winClock;
  unsigned char counter[4];         /* the change counter the cache matches */
  int counterValid;
  int isMainDb;
  unsigned hiblk;                   /* allocated blocks, as last seen */
  int chunk;                        /* extension, blocks */
  char *zDelete;                    /* delete this name at close */
  char *zPath;                      /* the full name (for the lock file) */
  unsigned lastStatus;              /* last failing VMS status */
};

static VmsLockFile *lockFiles;
static VmsFile *openFiles;

/* Diagnostics: with the logical name VMSSQLITE_TRACE naming a directory,
** each process logs its lock calls and syncs to <dir>VMSSQLITE_<pid>.LOG. */
static FILE *traceFile;
static unsigned long nQioR, nQioW, nAcp, nXRead, nXWrite;   /* counted when tracing */
static void trace(const char *fmt, ...)
{
  va_list ap;
  struct timeval tv;
  if (!traceFile) return;
  gettimeofday(&tv, 0);
  fprintf(traceFile, "%02d:%02d.%03d ", (int) (tv.tv_sec / 60 % 60), (int) (tv.tv_sec % 60),
          (int) (tv.tv_usec / 1000));
  va_start(ap, fmt);
  vfprintf(traceFile, fmt, ap);
  va_end(ap);
  fputc('\n', traceFile);
  fflush(traceFile);
}
static const char *lockName(int e)
{
  static const char *n[] = { "NONE", "SHARED", "RESERVED", "PENDING", "EXCLUSIVE" };
  return e >= 0 && e <= 4 ? n[e] : "?";
}

/* 32-bit memory: _malloc32 with 64-bit pointers (clang, which does not
** define __INITIAL_POINTER_SIZE), malloc with VSI C's 32-bit pointers */
#if defined(__clang__) || (defined(__INITIAL_POINTER_SIZE) && __INITIAL_POINTER_SIZE == 64)
#  define vmsMalloc32 _malloc32
#else
#  define vmsMalloc32 malloc
#endif

static void *alloc32(size_t n)
{
  void *p = vmsMalloc32(n);
  if (p && (unsigned long) p >= 0x80000000UL) {     /* RMS would refuse it */
    sqlite3_log(SQLITE_NOMEM, "vms: no 32-bit memory for RMS blocks");
    free(p);
    return 0;
  }
  if (p) memset(p, 0, n);
  return p;
}

static sqlite3_mutex *vfsMutex(void)
{
  return sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_VFS1);
}

static int vmsError(VmsFile *p, int rc, unsigned st, const char *what)
{
  if (p) p->lastStatus = st;
  sqlite3_log(rc, "vms: %s: %%X%08X%s%s", what, st, p && p->zPath ? " " : "",
              p && p->zPath ? p->zPath : "");
  return rc;
}

/*
** Names.  SQLite passes Unix-style names (from applications such as PHP) or
** VMS file specifications.  Unix-style names are converted by the C RTL;
** RMS then fills in the device and directory.  The result has no version,
** so SQLite can append "-journal" etc. to it.
*/
static char toVmsBuf[VMS_MAXPATH + 1];
static int toVmsCapture(char *name, int type)   /* the first name is enough: stop */
{
  (void) type;
  strncpy(toVmsBuf, name, VMS_MAXPATH);
  toVmsBuf[VMS_MAXPATH] = 0;
  return 0;
}

/* Fill io->name from zName (Unix or VMS syntax).  Caller holds the mutex
** when it may convert (toVmsBuf is static). */
static int setName(VmsIo *io, const char *zName)
{
  if (strchr(zName, '/')) {
    toVmsBuf[0] = 0;
    if (decc$to_vms(zName, toVmsCapture, 0, 0) <= 0 && !toVmsBuf[0]) return SQLITE_CANTOPEN;
    zName = toVmsBuf;
  }
  if (strlen(zName) > VMS_MAXPATH) return SQLITE_CANTOPEN;
  strcpy(io->name, zName);
  return SQLITE_OK;
}

static void initFab(VmsIo *io)
{
  io->fab = cc$rms_fab;
  io->naml = cc$rms_naml;
  io->fab.fab$l_fna = (char *) -1;          /* the name is in the NAML */
  io->fab.fab$b_fns = 0;
  io->fab.fab$l_dna = "SYS$DISK:[]";
  io->fab.fab$b_dns = 11;
  io->fab.fab$l_naml = &io->naml;
  io->naml.naml$l_long_filename = io->name;
  io->naml.naml$l_long_filename_size = strlen(io->name);
  io->naml.naml$l_long_expand = io->esa;
  io->naml.naml$l_long_expand_alloc = VMS_MAXPATH;
  io->naml.naml$l_long_result = io->rsa;
  io->naml.naml$l_long_result_alloc = VMS_MAXPATH;
}

/* The expanded (or resultant) name without its version. */
static void nameNoVersion(VmsIo *io, int result, char *out, int nOut)
{
  const char *s = result ? io->rsa : io->esa;
  int n = result ? io->naml.naml$l_long_result_size : io->naml.naml$l_long_expand_size;
  int nver = io->naml.naml$l_long_ver_size;
  n -= nver;
  if (n < 0) n = 0;
  if (n >= nOut) n = nOut - 1;
  memcpy(out, s, n);
  out[n] = 0;
}

/*
** The file header: record attributes (end of file, allocation) and extension.
*/
static unsigned acp(VmsFile *p, unsigned func, int withAtr, int exctl, unsigned exsz, unsigned exvbn)
{
  VmsIo *io = p->io;
  unsigned st;
  memset(&io->fib, 0, sizeof io->fib);
  memcpy(&io->fib.fib$w_fid, p->fid, sizeof p->fid);
  io->fib.fib$w_exctl = exctl;
  io->fib.fib$l_exsz = exsz;
  io->fib.fib$l_exvbn = exvbn;
  io->fibd.dsc$w_length = sizeof io->fib;
  io->fibd.dsc$b_dtype = DSC$K_DTYPE_Z;
  io->fibd.dsc$b_class = DSC$K_CLASS_S;
  io->fibd.dsc$a_pointer = (char *) &io->fib;
  io->atr[0].atr$w_size = ATR$S_RECATTR;
  io->atr[0].atr$w_type = ATR$C_RECATTR;
  io->atr[0].atr$l_addr = &io->fat;
  memset(&io->atr[1], 0, sizeof io->atr[1]);
  nAcp++;
  st = sys$qiow(EFN$C_ENF, p->chan, func, (struct _iosb *) io->iosb, 0, 0, &io->fibd, 0, 0, 0,
                withAtr ? (__int64) io->atr : 0, 0);
  if (st & 1) st = io->iosb[0];
  return st;
}

static unsigned readAttr(VmsFile *p)
{
  unsigned st = acp(p, IO$_ACCESS, 1, 0, 0, 0);
  if (st & 1) {
    FAT *f = &p->io->fat;
    unsigned efblk = ((unsigned) f->fat$w_efblkh << 16) | f->fat$w_efblkl;
    p->hiblk = ((unsigned) f->fat$w_hiblkh << 16) | f->fat$w_hiblkl;
    p->eof = efblk ? (i64) (efblk - 1) * VMS_BLOCK + f->fat$w_ffbyte : 0;
    p->dEof = p->eof;
  }
  return st;
}

/* Write p->eof into the header. */
static unsigned writeEof(VmsFile *p)
{
  unsigned st = acp(p, IO$_ACCESS, 1, 0, 0, 0);
  if (st & 1) {
    FAT *f = &p->io->fat;
    unsigned eb = (unsigned) (p->eof / VMS_BLOCK) + 1;
    f->fat$w_efblkh = eb >> 16;
    f->fat$w_efblkl = eb & 0xFFFF;
    f->fat$w_ffbyte = p->eof % VMS_BLOCK;
    st = acp(p, IO$_MODIFY, 1, 0, 0, 0);
  }
  if (st & 1) p->eofDirty = 0;
  return st;
}

static int flushEof(VmsFile *p)
{
  if (p->eofDirty) {
    unsigned st = writeEof(p);
    if (!(st & 1)) return vmsError(p, SQLITE_IOERR_FSYNC, st, "set end of file");
  }
  return SQLITE_OK;
}

/* Make sure blocks 1..nblk are allocated. */
static int allocate(VmsFile *p, unsigned nblk)
{
  unsigned st, want;
  if (nblk <= p->hiblk) return SQLITE_OK;
  st = acp(p, IO$_ACCESS, 1, 0, 0, 0);          /* another process may have extended */
  if (st & 1) p->hiblk = ((unsigned) p->io->fat.fat$w_hiblkh << 16) | p->io->fat.fat$w_hiblkl;
  if (nblk <= p->hiblk) return SQLITE_OK;
  want = nblk - p->hiblk;
  if (want < (unsigned) p->chunk) want = p->chunk;
  st = acp(p, IO$_MODIFY, 0, FIB$M_EXTEND, want, 0);
  if (!(st & 1)) {
    want = nblk - p->hiblk;                     /* disk nearly full: just what is needed */
    st = acp(p, IO$_MODIFY, 0, FIB$M_EXTEND, want, 0);
  }
  if (!(st & 1)) return vmsError(p, SQLITE_FULL, st, "extend");
  st = acp(p, IO$_ACCESS, 1, 0, 0, 0);
  if (st & 1) p->hiblk = ((unsigned) p->io->fat.fat$w_hiblkh << 16) | p->io->fat.fat$w_hiblkl;
  return SQLITE_OK;
}

/* Whole blocks: off and n multiples of VMS_BLOCK. */
static unsigned blockIo(VmsFile *p, int write, void *buf, i64 n, i64 off)
{
  char *b = buf;
  while (n > 0) {
    unsigned len = n > VMS_XFER ? VMS_XFER : (unsigned) n;
    unsigned short iosb[4];
    unsigned st;
    if (write) nQioW++; else nQioR++;
    st = sys$qiow(EFN$C_ENF, p->chan, write ? IO$_WRITEVBLK : IO$_READVBLK,
                           (struct _iosb *) iosb, 0, 0, b, len, (unsigned) (off / VMS_BLOCK) + 1,
                           0, 0, 0);
    if (st & 1) st = iosb[0];
    if (!(st & 1)) return st;
    b += len; off += len; n -= len;
  }
  return SS$_NORMAL;
}

/*
** sqlite3_io_methods
*/
static int vmsLockRelease(VmsFile *p, int eLock);
static int flushWbuf(VmsFile *p);

static int vmsClose(sqlite3_file *id)
{
  VmsFile *p = (VmsFile *) id;
  int rc = flushWbuf(p);
  if (rc == SQLITE_OK) rc = flushEof(p);
  sqlite3_free(p->wins);
  p->wins = 0;
  sqlite3_mutex_enter(vfsMutex());
  if (p->pPrev) p->pPrev->pNext = p->pNext; else if (openFiles == p) openFiles = p->pNext;
  if (p->pNext) p->pNext->pPrev = p->pPrev;
  p->pNext = p->pPrev = 0;
  sqlite3_mutex_leave(vfsMutex());
  if (p->lf) {
    vmsLockRelease(p, NO_LOCK);
    sqlite3_mutex_enter(vfsMutex());
    if (--p->lf->nRef == 0) {
      VmsLockFile **pp;
      for (pp = &lockFiles; *pp && *pp != p->lf; pp = &(*pp)->next) ;
      if (*pp) *pp = p->lf->next;
      close(p->lf->fd);
      sqlite3_free(p->lf);
    }
    sqlite3_mutex_leave(vfsMutex());
    p->lf = 0;
  }
  if (p->chan) sys$dassgn(p->chan);
  p->chan = 0;
  if (p->zDelete) {
    sqlite3_vfs *v = sqlite3_vfs_find("vms");
    if (v) v->xDelete(v, p->zDelete, 0);
    sqlite3_free(p->zDelete);
  }
  sqlite3_free(p->zPath);
  if (p->io) free(p->io);
  p->io = 0;
  return rc;
}

typedef unsigned long long u64;
#define BIT(b) ((u64) 1 << (b))

static int cacheInit(VmsFile *p)
{
  int i;
  if (p->wins) return 1;
  p->wins = sqlite3_malloc(VMS_NWIN * VMS_XFER + VMS_XFER);
  if (!p->wins) return 0;
  p->scratch = p->wins + VMS_NWIN * VMS_XFER;
  for (i = 0; i < VMS_NWIN; i++) { p->winOff[i] = -1; p->winValid[i] = p->winDirty[i] = 0; }
  return 1;
}

/* Clean windows overlapping [off, off+n) are forgotten (dirty ones are
** flushed first by every caller). */
static void cacheDrop(VmsFile *p, i64 off, i64 n)
{
  int i;
  if (!p->wins) return;
  for (i = 0; i < VMS_NWIN; i++)
    if (p->winOff[i] >= 0 && p->winOff[i] < off + n && p->winOff[i] + VMS_XFER > off)
      p->winOff[i] = -1;
}

static void cacheDropAll(VmsFile *p)
{
  int i;
  if (!p->wins) return;
  for (i = 0; i < VMS_NWIN; i++) p->winOff[i] = -1;
}

/* Write window i's dirty blocks, one $QIO per run. */
static int flushWin(VmsFile *p, int i)
{
  u64 d = p->winDirty[i];
  i64 w = p->winOff[i];
  int b, e, last, rc;
  unsigned st;
  if (!d) return SQLITE_OK;
  for (last = WIN_BLOCKS - 1; !(d & BIT(last)); last--) ;
  rc = allocate(p, (unsigned) (w / VMS_BLOCK) + last + 1);
  if (rc) return rc;
  for (b = 0; b < WIN_BLOCKS; b = e) {
    i64 runEnd;
    if (!(d & BIT(b))) { e = b + 1; continue; }
    for (e = b; e < WIN_BLOCKS && (d & BIT(e)); e++) ;
    st = blockIo(p, 1, p->wins + (i64) i * VMS_XFER + b * VMS_BLOCK, (i64) (e - b) * VMS_BLOCK,
                 w + (i64) b * VMS_BLOCK);
    if (!(st & 1)) return vmsError(p, SQLITE_IOERR_WRITE, st, "write");
    runEnd = w + (i64) e * VMS_BLOCK;
    if (runEnd > p->eof) runEnd = p->eof;
    if (runEnd > p->dEof) p->dEof = runEnd;
  }
  p->winDirty[i] = 0;
  return SQLITE_OK;
}

/* Every dirty block, in file order. */
static int flushWbuf(VmsFile *p)
{
  int rc;
  if (!p->wins) return SQLITE_OK;
  for (;;) {
    int i, m = -1;
    for (i = 0; i < VMS_NWIN; i++)
      if (p->winDirty[i] && (m < 0 || p->winOff[i] < p->winOff[m])) m = i;
    if (m < 0) return SQLITE_OK;
    if ((rc = flushWin(p, m)) != SQLITE_OK) return rc;
  }
}

/* The window for offset w (a multiple of VMS_XFER): found, or an empty or
** least recently used one (flushed first) given to w.  -1 on error. */
static int cacheWin(VmsFile *p, i64 w, int *pRc)
{
  int i, v = -1;
  for (i = 0; i < VMS_NWIN; i++)
    if (p->winOff[i] == w) { p->winUse[i] = ++p->winClock; return i; }
  for (i = 0; i < VMS_NWIN && v < 0; i++)
    if (p->winOff[i] < 0) v = i;
  if (v < 0) {
    for (v = 0, i = 1; i < VMS_NWIN; i++)
      if (p->winUse[i] < p->winUse[v]) v = i;
    if ((*pRc = flushWin(p, v)) != SQLITE_OK) return -1;
  }
  p->winOff[v] = w;
  p->winValid[v] = p->winDirty[v] = 0;
  p->winUse[v] = ++p->winClock;
  return v;
}

/* Make the blocks in mask valid in window i: those on the disk with one
** $QIO (into scratch, so as not to overwrite blocks already valid), the rest
** (past the end of what is on the disk) zeros. */
static int ensureValid(VmsFile *p, int i, u64 mask)
{
  u64 miss = mask & ~p->winValid[i], disk;
  i64 w = p->winOff[i], nd = (p->dEof - w + VMS_BLOCK - 1) / VMS_BLOCK;
  char *win = p->wins + (i64) i * VMS_XFER;
  int b, first, last;
  unsigned st;
  if (!miss) return SQLITE_OK;
  if (nd < 0) nd = 0;
  if (nd > WIN_BLOCKS) nd = WIN_BLOCKS;
  disk = nd >= WIN_BLOCKS ? ~(u64) 0 : BIT(nd) - 1;
  if (miss & disk) {
    for (first = 0; !(miss & disk & BIT(first)); first++) ;
    for (last = WIN_BLOCKS - 1; !(miss & disk & BIT(last)); last--) ;
    st = blockIo(p, 0, p->scratch, (i64) (last - first + 1) * VMS_BLOCK, w + (i64) first * VMS_BLOCK);
    if (!(st & 1)) return vmsError(p, SQLITE_IOERR_READ, st, "read");
    for (b = first; b <= last; b++)
      if (!(p->winValid[i] & BIT(b))) {
        memcpy(win + b * VMS_BLOCK, p->scratch + (b - first) * VMS_BLOCK, VMS_BLOCK);
        p->winValid[i] |= BIT(b);
        /* bytes of the last disk block past the end of file: zeros */
        if (p->dEof % VMS_BLOCK && (p->dEof - w) / VMS_BLOCK == b)
          memset(win + (p->dEof - w), 0, VMS_BLOCK - (size_t) (p->dEof % VMS_BLOCK));
      }
  }
  for (b = 0; b < WIN_BLOCKS; b++)
    if (miss & ~disk & BIT(b)) { memset(win + b * VMS_BLOCK, 0, VMS_BLOCK); p->winValid[i] |= BIT(b); }
  return SQLITE_OK;
}

/* Is the file as this connection last saw it?  One $QIO, no ACP call.
** The change counter alone does not tell: rolling back a hot journal puts
** the old page 1, counter included, back and truncates what the dead
** writer added.  So the header's page count (bytes 28-31, valid when bytes
** 92-95 match the counter) times its page size must also be our end of
** file.  (A dead writer's additions not yet rolled back can leave our end of
** file short; SQLite rolls back before it reads, and writeLock reads it.) */
static int sameFile(VmsFile *p)
{
  const unsigned char *h = (const unsigned char *) p->io->edge;
  unsigned st, pgsz;
  i64 nPage;
  if (!p->wins || !p->counterValid || p->dEof < 100) return 0;
  st = blockIo(p, 0, p->io->edge, VMS_BLOCK, 0);
  if (!(st & 1) || memcmp(p->counter, h + 24, 4) || memcmp(h + 24, h + 92, 4)) return 0;
  pgsz = (unsigned) h[16] << 8 | h[17];
  if (pgsz == 1) pgsz = 65536;
  nPage = (i64) h[28] << 24 | h[29] << 16 | h[30] << 8 | h[31];
  return nPage * pgsz == p->eof;
}

/* A new read transaction on the database: keep the cache only if no other
** process has committed since it was filled. */
static void cacheValidate(VmsFile *p)
{
  unsigned st;
  if (!p->wins) return;
  if (p->dEof < 28) { cacheDropAll(p); p->counterValid = 0; return; }
  st = blockIo(p, 0, p->io->edge, VMS_BLOCK, 0);
  if (!(st & 1) || !p->counterValid || memcmp(p->counter, p->io->edge + 24, 4)) cacheDropAll(p);
  if (st & 1) { memcpy(p->counter, p->io->edge + 24, 4); p->counterValid = 1; }
  else p->counterValid = 0;
}

/* After our own commit (flushed): the cache matches the file under its new
** change counter. */
static void cacheRemember(VmsFile *p)
{
  unsigned st;
  if (!p->wins || p->dEof < 28) return;
  st = blockIo(p, 0, p->io->edge, VMS_BLOCK, 0);
  if (st & 1) { memcpy(p->counter, p->io->edge + 24, 4); p->counterValid = 1; }
  else { cacheDropAll(p); p->counterValid = 0; }
}

static int vmsRead(sqlite3_file *id, void *buf, int amt, i64 off)
{
  VmsFile *p = (VmsFile *) id;
  i64 end = off + amt, have;
  char *dst = buf;
  unsigned st;
  int rc;

  nXRead++;
  if (end > p->eof && !p->eofDirty && !p->eofLocked) {     /* the header may know better */
    st = readAttr(p);
    if (!(st & 1)) return vmsError(p, SQLITE_IOERR_READ, st, "read attributes");
  }
  have = p->eof - off;
  if (have <= 0) { memset(buf, 0, amt); return SQLITE_IOERR_SHORT_READ; }
  if (have > amt) have = amt;
  if (!cacheInit(p)) return SQLITE_IOERR_NOMEM;
  while (have > 0) {
    i64 w = off - off % VMS_XFER, k, nb;
    int i = cacheWin(p, w, &rc);
    if (i < 0) return rc;
    nb = (p->eof - w + VMS_BLOCK - 1) / VMS_BLOCK;      /* the whole window: read ahead */
    if (nb > WIN_BLOCKS) nb = WIN_BLOCKS;
    if ((rc = ensureValid(p, i, nb >= WIN_BLOCKS ? ~(u64) 0 : BIT(nb) - 1)) != SQLITE_OK) return rc;
    k = w + VMS_XFER - off;
    if (k > have) k = have;
    memcpy(dst, p->wins + (i64) i * VMS_XFER + (off - w), k);
    dst += k; off += k; have -= k;
  }
  if (dst - (char *) buf < amt) {
    memset(dst, 0, amt - (dst - (char *) buf));
    return SQLITE_IOERR_SHORT_READ;
  }
  return SQLITE_OK;
}

/* Read-modify-write one partial block on the disk: n bytes of src at off. */
static unsigned editBlock(VmsFile *p, const char *src, int n, i64 off)
{
  i64 b = off - off % VMS_BLOCK;
  unsigned st = SS$_NORMAL;
  if (b < p->dEof) st = blockIo(p, 0, p->io->edge, VMS_BLOCK, b);
  else memset(p->io->edge, 0, VMS_BLOCK);
  if (!(st & 1)) return st;
  if (b < p->dEof && p->dEof - b < VMS_BLOCK)  /* bytes past the end of file are zeros */
    memset(p->io->edge + (p->dEof - b), 0, VMS_BLOCK - (p->dEof - b));
  memcpy(p->io->edge + (off - b), src, n);
  return blockIo(p, 1, p->io->edge, VMS_BLOCK, b);
}

/* Write to the disk now, bypassing the cache (the caller has flushed it):
** for a write that leaves a gap past the end, which must be zeroed. */
static int writeThrough(VmsFile *p, const char *src, int amt, i64 off)
{
  i64 end = off + amt, mid;
  unsigned st;
  int rc;

  rc = allocate(p, (unsigned) ((end + VMS_BLOCK - 1) / VMS_BLOCK));
  if (rc) return rc;
  if (off > p->dEof) {
    /* newly allocated blocks hold whatever was on the disk: zeros up to off */
    static const char z[VMS_XFER];
    i64 g = p->dEof, gend = off - off % VMS_BLOCK;
    if (g % VMS_BLOCK) {                        /* the rest of the old last block */
      int k = VMS_BLOCK - (int) (g % VMS_BLOCK);
      if (g + k > off) k = (int) (off - g);
      st = editBlock(p, z, k, g);
      if (!(st & 1)) return vmsError(p, SQLITE_IOERR_WRITE, st, "write");
      g += k;
    }
    while (g < gend) {                          /* whole blocks */
      i64 n = gend - g > VMS_XFER ? VMS_XFER : gend - g;
      st = blockIo(p, 1, (void *) z, n, g);
      if (!(st & 1)) return vmsError(p, SQLITE_IOERR_WRITE, st, "write");
      g += n;
    }
    if (g > p->dEof) p->dEof = g;               /* editBlock below sees zeros past here */
  }
  /* head */
  if (off % VMS_BLOCK || amt < VMS_BLOCK) {
    int k = VMS_BLOCK - (int) (off % VMS_BLOCK);
    if (k > amt) k = amt;
    st = editBlock(p, src, k, off);
    if (!(st & 1)) return vmsError(p, SQLITE_IOERR_WRITE, st, "write");
    src += k; off += k; amt -= k;
  }
  /* middle */
  mid = amt - amt % VMS_BLOCK;
  if (mid > 0) {
    st = blockIo(p, 1, (void *) src, mid, off);
    if (!(st & 1)) return vmsError(p, SQLITE_IOERR_WRITE, st, "write");
    src += mid; off += mid; amt -= (int) mid;
  }
  /* tail */
  if (amt > 0) {
    st = editBlock(p, src, amt, off);
    if (!(st & 1)) return vmsError(p, SQLITE_IOERR_WRITE, st, "write");
  }
  if (end > p->dEof) p->dEof = end;
  if (end > p->eof) { p->eof = end; p->eofDirty = 1; }
  return SQLITE_OK;
}

static int vmsWrite(sqlite3_file *id, const void *buf, int amt, i64 off)
{
  VmsFile *p = (VmsFile *) id;
  const char *src = buf;
  i64 end = off + amt;
  int rc;

  nXWrite++;
  if (!p->eofDirty && !p->eofLocked && off > p->eof) {     /* past our end: refresh it */
    unsigned st = readAttr(p);
    if (!(st & 1)) return vmsError(p, SQLITE_IOERR_WRITE, st, "read attributes");
  }
  if (off > p->eof || !cacheInit(p)) {          /* a gap past the end: to the disk */
    i64 from = p->dEof < off ? p->dEof : off;
    if ((rc = flushWbuf(p)) != SQLITE_OK) return rc;
    rc = writeThrough(p, src, amt, off);
    cacheDrop(p, from, end - from);
    return rc;
  }
  while (amt > 0) {
    i64 w = off - off % VMS_XFER, pend;
    int i = cacheWin(p, w, &rc), b0, b1, b, k;
    u64 edges = 0;
    if (i < 0) return rc;
    pend = w + VMS_XFER < off + amt ? w + VMS_XFER : off + amt;
    b0 = (int) ((off - w) / VMS_BLOCK);
    b1 = (int) ((pend - 1 - w) / VMS_BLOCK);
    if ((off - w) % VMS_BLOCK) edges |= BIT(b0);          /* partial blocks: */
    if ((pend - w) % VMS_BLOCK) edges |= BIT(b1);         /*   their old bytes first */
    if (edges && (rc = ensureValid(p, i, edges)) != SQLITE_OK) return rc;
    k = (int) (pend - off);
    memcpy(p->wins + (i64) i * VMS_XFER + (off - w), src, k);
    for (b = b0; b <= b1; b++) { p->winValid[i] |= BIT(b); p->winDirty[i] |= BIT(b); }
    src += k; off += k; amt -= k;
  }
  if (end > p->eof) { p->eof = end; p->eofDirty = 1; }
  return SQLITE_OK;
}

static int vmsTruncate(sqlite3_file *id, i64 size)
{
  VmsFile *p = (VmsFile *) id;
  unsigned st, keep;
  int rc;
  if ((rc = flushWbuf(p)) != SQLITE_OK) return rc;
  if (traceFile) trace("truncate %s %lld -> %lld", p->zPath ? p->zPath : "temp", (long long) p->eof,
                       (long long) size);
  cacheDropAll(p);
  if (size > p->eof) {                          /* growing: allocate, the blocks are zeroed */
    rc = vmsWrite(id, "", 0, size);
    if (rc) return rc;
  }
  p->eof = size;
  if (p->dEof > size) p->dEof = size;
  st = writeEof(p);
  if (!(st & 1)) return vmsError(p, SQLITE_IOERR_TRUNCATE, st, "truncate");
  /* give back the allocation past the end; another accessor may prevent it */
  keep = (unsigned) ((size + VMS_BLOCK - 1) / VMS_BLOCK);
  if (keep + (unsigned) p->chunk < p->hiblk) {
    st = acp(p, IO$_MODIFY, 0, FIB$M_TRUNC, 0, keep + 1);
    if (st & 1) readAttr(p);
  }
  return SQLITE_OK;
}

static int vmsSync(sqlite3_file *id, int flags)
{
  /* XFC writes through: a completed $QIO write is on the disk.  What is left
  ** is the end of file. */
  VmsFile *p = (VmsFile *) id;
  int rc = flushWbuf(p);
  (void) flags;
  if (traceFile) trace("sync %s eof %lld", p->zPath ? p->zPath : "temp", (long long) p->eof);
  return rc ? rc : flushEof(p);
}

static int vmsFileSize(sqlite3_file *id, i64 *pSize)
{
  VmsFile *p = (VmsFile *) id;
  if (!p->eofDirty && !p->eofLocked) {
    unsigned st = readAttr(p);
    if (!(st & 1)) return vmsError(p, SQLITE_IOERR_FSTAT, st, "read attributes");
  }
  *pSize = p->eof;
  return SQLITE_OK;
}

/*
** Locks.  Every fcntl() lock is released with exactly the range locked.
*/
static int lk(VmsLockFile *lf, int type, long start, long len)
{
  struct flock l;
  memset(&l, 0, sizeof l);
  l.l_type = type;
  l.l_whence = SEEK_SET;
  l.l_start = start;
  l.l_len = len;
  return fcntl(lf->fd, F_SETLK, &l);
}

/* Change the SHARED range to F_RDLCK, F_WRLCK or 0 (none): release, then take. */
static int setShared(VmsLockFile *lf, int type)
{
  if (lf->shared == type) return 0;
  if (lf->shared) {
    lk(lf, F_UNLCK, SHARED_FIRST, SHARED_SIZE);
    lf->shared = 0;
  }
  if (type) {
    if (lk(lf, type, SHARED_FIRST, SHARED_SIZE)) return -1;
    lf->shared = type;
  }
  return 0;
}

/* Attach the process's record for "<database>-lock", opening it if needed.
** Caller holds the mutex. */
static int attachLockFile(VmsFile *p)
{
  char *zLock;
  struct stat st;
  int fd;
  VmsLockFile *lf;

  zLock = sqlite3_mprintf("%s-lock", p->zPath);
  if (!zLock) return SQLITE_NOMEM;
  /* mode 0: the process's default protection, as RMS gives the database;
  ** "shr": other processes (whatever their C RTL features) share it */
  fd = open(zLock, O_RDWR | O_CREAT, 0, "shr=get,put,upd");
  if (fd < 0) fd = open(zLock, O_RDONLY, 0, "shr=get,put,upd");
  if (fd < 0) {
    vmsError(p, SQLITE_IOERR_LOCK, errno == EVMSERR ? vaxc$errno : errno, "open lock file");
    sqlite3_free(zLock);
    return SQLITE_IOERR_LOCK;
  }
  sqlite3_free(zLock);
  if (fstat(fd, &st)) { close(fd); return SQLITE_IOERR_LOCK; }
  for (lf = lockFiles; lf; lf = lf->next)
    if (lf->dev == st.st_dev && lf->ino == st.st_ino) break;
  if (lf) {
    close(fd);          /* would drop the process's locks?  no: only lf->fd holds any */
  } else {
    lf = sqlite3_malloc(sizeof *lf);
    if (!lf) { close(fd); return SQLITE_NOMEM; }
    memset(lf, 0, sizeof *lf);
    lf->dev = st.st_dev;
    lf->ino = st.st_ino;
    lf->fd = fd;
    lf->next = lockFiles;
    lockFiles = lf;
  }
  lf->nRef++;
  p->lf = lf;
  return SQLITE_OK;
}

static int vmsLock2(sqlite3_file *id, int eLock);
static int vmsLock(sqlite3_file *id, int eLock)
{
  VmsFile *p = (VmsFile *) id;
  int from = p->eLock, rc = vmsLock2(id, eLock);
  if (traceFile)
    trace("lock %s %s->%s rc %d (process %s, shared %d)", p->zPath ? p->zPath : "?",
          lockName(from), lockName(eLock), rc, p->lf ? lockName(p->lf->eLock) : "-",
          p->lf ? p->lf->nShared : 0);
  return rc;
}

/* SHARED -> RESERVED or EXCLUSIVE: this connection will write.  A writer
** killed in a cache spill (no commit, so the counter is unchanged) may have
** extended the file: read the end of file again, so that SQLite's rollback
** of its journal truncates what it added.  The cache holds no dirty blocks
** of the database here. */
static int writeLock(VmsFile *p)
{
  i64 was = p->eof;
  unsigned st;
  if (!p->isMainDb || p->eofDirty) return SQLITE_OK;
  st = readAttr(p);
  if (!(st & 1)) return vmsError(p, SQLITE_IOERR_LOCK, st, "read attributes");
  if (traceFile) trace("write lock: eof %lld -> %lld", (long long) was, (long long) p->eof);
  if (p->eof != was) cacheDropAll(p);
  p->eofLocked = 1;
  return SQLITE_OK;
}

static int vmsLock2(sqlite3_file *id, int eLock)
{
  VmsFile *p = (VmsFile *) id;
  VmsLockFile *lf;
  int rc = SQLITE_OK, wasReserved;
  unsigned st;
  i64 was;

  if (p->eLock >= eLock) return SQLITE_OK;
  sqlite3_mutex_enter(vfsMutex());
  if (!p->lf && (rc = attachLockFile(p)) != SQLITE_OK) goto out;
  lf = p->lf;

  /* another connection of this process holds PENDING or more */
  if (p->eLock != lf->eLock && (lf->eLock >= PENDING_LOCK || eLock > SHARED_LOCK)) {
    rc = SQLITE_BUSY;
    goto out;
  }
  /* SHARED wanted, and this process already has SHARED or RESERVED */
  if (eLock == SHARED_LOCK && (lf->eLock == SHARED_LOCK || lf->eLock == RESERVED_LOCK)) {
    p->eLock = SHARED_LOCK;
    lf->nShared++;
    goto refreshed;
  }

  if (eLock == SHARED_LOCK) {
    /* a reader takes PENDING briefly, so a waiting writer keeps new readers out */
    if (lk(lf, F_RDLCK, PENDING_BYTE, 1)) { rc = SQLITE_BUSY; goto out; }
    if (setShared(lf, F_RDLCK)) rc = SQLITE_BUSY;
    lk(lf, F_UNLCK, PENDING_BYTE, 1);
    if (rc) goto out;
    p->eLock = lf->eLock = SHARED_LOCK;
    lf->nShared++;
    goto refreshed;
  }
  if (eLock == EXCLUSIVE_LOCK && lf->nShared > 1) {   /* other connections here read */
    rc = SQLITE_BUSY;
    goto out;
  }
  if (eLock == RESERVED_LOCK) {
    if (lk(lf, F_WRLCK, RESERVED_BYTE, 1)) { rc = SQLITE_BUSY; goto out; }
    lf->reserved = 1;
    p->eLock = lf->eLock = RESERVED_LOCK;
    rc = writeLock(p);
    goto out;
  }
  /* EXCLUSIVE (SQLite asks for it from RESERVED, or again from PENDING) */
  if (!lf->pending) {
    if (lk(lf, F_WRLCK, PENDING_BYTE, 1)) { rc = SQLITE_BUSY; goto out; }
    lf->pending = 1;
  }
  /* PENDING keeps new readers out and RESERVED other writers, so the read
  ** lock can be dropped and the write lock tried without a conversion */
  if (setShared(lf, F_WRLCK)) {
    rc = setShared(lf, F_RDLCK) ? SQLITE_IOERR_LOCK : SQLITE_BUSY;
    p->eLock = lf->eLock = PENDING_LOCK;
    goto out;
  }
  /* not from RESERVED (which read it): from SHARED, to roll back a hot
  ** journal, or again after a busy PENDING */
  wasReserved = p->eLock == RESERVED_LOCK;
  p->eLock = lf->eLock = EXCLUSIVE_LOCK;
  if (!wasReserved) rc = writeLock(p);
  goto out;

refreshed:
  /* a new read transaction: the file may have changed size and content */
  if (p->eofDirty) goto out;
  if (p->isMainDb && sameFile(p)) {             /* no commit: cache and eof stand */
    p->eofLocked = 1;
    goto out;
  }
  was = p->eof;
  st = readAttr(p);
  if (!(st & 1)) { rc = vmsError(p, SQLITE_IOERR_RDLOCK, st, "read attributes"); goto out; }
  if (p->isMainDb) {
    if (traceFile) trace("read lock: eof %lld -> %lld", (long long) was, (long long) p->eof);
    if (p->eof != was) cacheDropAll(p);         /* blocks past the old end: not zeros */
    cacheValidate(p);
    p->eofLocked = 1;
  }
out:
  sqlite3_mutex_leave(vfsMutex());
  return rc;
}

static int vmsLockRelease(VmsFile *p, int eLock)
{
  VmsLockFile *lf = p->lf;
  int rc = SQLITE_OK;
  if (!lf || p->eLock <= eLock) return SQLITE_OK;
  if (p->eLock > SHARED_LOCK) {                 /* before others can look */
    rc = flushWbuf(p);
    if (rc == SQLITE_OK) rc = flushEof(p);
    /* our own commit changed the counter: the cache still matches the file */
    if (rc == SQLITE_OK && p->isMainDb) cacheRemember(p);
  }
  sqlite3_mutex_enter(vfsMutex());
  if (p->eLock > SHARED_LOCK) {
    if (eLock == SHARED_LOCK && setShared(lf, F_RDLCK)) rc = SQLITE_IOERR_RDLOCK;
    if (lf->pending) { lk(lf, F_UNLCK, PENDING_BYTE, 1); lf->pending = 0; }
    if (lf->reserved) { lk(lf, F_UNLCK, RESERVED_BYTE, 1); lf->reserved = 0; }
    lf->eLock = SHARED_LOCK;
  }
  if (eLock == NO_LOCK) {
    p->eofLocked = 0;
    if (--lf->nShared == 0) {
      setShared(lf, 0);
      lf->eLock = NO_LOCK;
    }
  }
  p->eLock = eLock;
  sqlite3_mutex_leave(vfsMutex());
  return rc;
}

static int vmsUnlock(sqlite3_file *id, int eLock)
{
  VmsFile *p = (VmsFile *) id;
  int from = p->eLock, rc = vmsLockRelease(p, eLock);
  if (traceFile && from > eLock) {
    trace("unlock %s %s->%s rc %d (xRead %lu xWrite %lu, $QIO read %lu write %lu, ACP %lu)",
          p->zPath ? p->zPath : "?", lockName(from), lockName(eLock), rc, nXRead, nXWrite,
          nQioR, nQioW, nAcp);
    nXRead = nXWrite = nQioR = nQioW = nAcp = 0;
  }
  return rc;
}

static int vmsCheckReservedLock(sqlite3_file *id, int *pResOut)
{
  VmsFile *p = (VmsFile *) id;
  int reserved = 0, rc = SQLITE_OK;
  sqlite3_mutex_enter(vfsMutex());
  if (!p->lf && (rc = attachLockFile(p)) != SQLITE_OK) goto out;
  if (p->lf->eLock > SHARED_LOCK) reserved = 1;
  else if (lk(p->lf, F_WRLCK, RESERVED_BYTE, 1)) reserved = 1;   /* someone has it */
  else lk(p->lf, F_UNLCK, RESERVED_BYTE, 1);
out:
  sqlite3_mutex_leave(vfsMutex());
  *pResOut = reserved;
  return rc;
}

static char *tempName(void);

static int vmsFileControl(sqlite3_file *id, int op, void *pArg)
{
  VmsFile *p = (VmsFile *) id;
  switch (op) {
  case SQLITE_FCNTL_LOCKSTATE:
    *(int *) pArg = p->eLock;
    return SQLITE_OK;
  case SQLITE_FCNTL_LAST_ERRNO:
    *(int *) pArg = (int) p->lastStatus;
    return SQLITE_OK;
  case SQLITE_FCNTL_CHUNK_SIZE:
    p->chunk = (*(int *) pArg + VMS_BLOCK - 1) / VMS_BLOCK;
    if (p->chunk < 1) p->chunk = 1;
    return SQLITE_OK;
  case SQLITE_FCNTL_SIZE_HINT:
    return allocate(p, (unsigned) ((*(i64 *) pArg + VMS_BLOCK - 1) / VMS_BLOCK));
  case SQLITE_FCNTL_VFSNAME:
    *(char **) pArg = sqlite3_mprintf("vms");
    return SQLITE_OK;
  case SQLITE_FCNTL_TEMPFILENAME:
    *(char **) pArg = tempName();
    return *(char **) pArg ? SQLITE_OK : SQLITE_NOMEM;
  case SQLITE_FCNTL_HAS_MOVED:
    *(int *) pArg = 0;
    return SQLITE_OK;
  }
  return SQLITE_NOTFOUND;
}

static int vmsSectorSize(sqlite3_file *id)
{
  (void) id;
  return VMS_BLOCK;
}

static int vmsDeviceCharacteristics(sqlite3_file *id)
{
  (void) id;
  return SQLITE_IOCAP_POWERSAFE_OVERWRITE;
}

static const sqlite3_io_methods vmsIoMethods = {
  1,
  vmsClose, vmsRead, vmsWrite, vmsTruncate, vmsSync, vmsFileSize,
  vmsLock, vmsUnlock, vmsCheckReservedLock, vmsFileControl,
  vmsSectorSize, vmsDeviceCharacteristics
};

/*
** sqlite3_vfs
*/
static unsigned randomState;

static char *tempName(void)
{
  unsigned r;
  const char *dir = sqlite3_temp_directory ? sqlite3_temp_directory : "SYS$SCRATCH:";
  sqlite3_randomness(sizeof r, &r);
  return sqlite3_mprintf("%sSQLITE_%08X_%08X.TMP", dir, (unsigned) getpid(), r);
}

static int vmsOpen(sqlite3_vfs *vfs, const char *zName, sqlite3_file *id, int flags, int *pOutFlags)
{
  VmsFile *p = (VmsFile *) id;
  VmsIo *io;
  int create = flags & SQLITE_OPEN_CREATE;
  int rw = flags & SQLITE_OPEN_READWRITE;
  int temp = zName == 0;
  unsigned st;
  int rc;

  (void) vfs;
  memset(p, 0, sizeof *p);
  io = p->io = alloc32(sizeof *io);
  if (!io) return SQLITE_NOMEM;
  p->chunk = VMS_CHUNK;
  if (temp) {
    /* no name: a file with no directory entry, deleted by the file system
    ** when the channel is deassigned */
    strcpy(io->name, "SQLITE.TMP");
    initFab(io);
    /* the default name must be in 32-bit memory too */
    strncpy(io->dna, sqlite3_temp_directory ? sqlite3_temp_directory : "SYS$SCRATCH:", VMS_MAXPATH);
    io->fab.fab$l_dna = io->dna;
    io->fab.fab$b_dns = strlen(io->dna) > 255 ? 255 : strlen(io->dna);
    io->fab.fab$l_fop = FAB$M_UFO | FAB$M_TMD | FAB$M_TMP;
    create = 1; rw = 1;
  } else {
    sqlite3_mutex_enter(vfsMutex());
    rc = setName(io, zName);
    sqlite3_mutex_leave(vfsMutex());
    if (rc) { free(io); p->io = 0; return rc; }
    initFab(io);
    io->fab.fab$l_fop = FAB$M_UFO | (create ? FAB$M_CIF : 0);
  }
  io->fab.fab$b_fac = FAB$M_GET | FAB$M_BIO | (rw ? FAB$M_PUT : 0);
  io->fab.fab$b_shr = FAB$M_SHRGET | FAB$M_SHRPUT | FAB$M_UPI;
  io->fab.fab$b_rfm = FAB$C_STMLF;              /* as the C RTL creates files */
  io->fab.fab$b_rat = FAB$M_CR;
  if (create && (flags & SQLITE_OPEN_EXCLUSIVE) && !temp) {
    st = sys$open(&io->fab);                    /* must not exist yet */
    if (st & 1) {
      sys$dassgn((unsigned short) io->fab.fab$l_stv);
      free(io); p->io = 0;
      return SQLITE_CANTOPEN;
    }
    initFab(io);
    io->fab.fab$l_fop = FAB$M_UFO | FAB$M_CIF;
    io->fab.fab$b_fac = FAB$M_GET | FAB$M_BIO | FAB$M_PUT;
    io->fab.fab$b_shr = FAB$M_SHRGET | FAB$M_SHRPUT | FAB$M_UPI;
    io->fab.fab$b_rfm = FAB$C_STMLF;
    io->fab.fab$b_rat = FAB$M_CR;
  }
  st = create ? sys$create(&io->fab) : sys$open(&io->fab);
  if (!(st & 1) && rw && (st == RMS$_PRV || io->fab.fab$l_stv == SS$_NOPRIV) && !temp) {
    /* no write access: open read-only, as the unix VFS does */
    initFab(io);
    io->fab.fab$l_fop = FAB$M_UFO;
    io->fab.fab$b_fac = FAB$M_GET | FAB$M_BIO;
    io->fab.fab$b_shr = FAB$M_SHRGET | FAB$M_SHRPUT | FAB$M_UPI;
    st = sys$open(&io->fab);
    rw = 0;
    flags = (flags & ~(SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)) | SQLITE_OPEN_READONLY;
  }
  if (!(st & 1)) {
    vmsError(p, SQLITE_CANTOPEN, st, zName ? zName : "temporary file");
    free(io); p->io = 0;
    return SQLITE_CANTOPEN;
  }
  p->chan = (unsigned short) io->fab.fab$l_stv;
  memcpy(p->fid, io->naml.naml$w_fid, sizeof p->fid);
  p->isReadonly = !rw;
  p->isMainDb = (flags & SQLITE_OPEN_MAIN_DB) != 0;
  if (zName) {
    char full[VMS_MAXPATH + 1];
    nameNoVersion(io, 1, full, sizeof full);
    p->zPath = sqlite3_mprintf("%s", full);
    if ((flags & SQLITE_OPEN_DELETEONCLOSE) && p->zPath) p->zDelete = sqlite3_mprintf("%s", full);
  }
  st = readAttr(p);
  if (!(st & 1)) {
    sys$dassgn(p->chan);
    free(io); p->io = 0;
    return vmsError(0, SQLITE_CANTOPEN, st, "read attributes");
  }
  if (pOutFlags) *pOutFlags = flags;
  p->base.pMethods = &vmsIoMethods;
  sqlite3_mutex_enter(vfsMutex());
  p->pNext = openFiles;
  if (openFiles) openFiles->pPrev = p;
  openFiles = p;
  sqlite3_mutex_leave(vfsMutex());
  return SQLITE_OK;
}

static int vmsDelete(sqlite3_vfs *vfs, const char *zName, int syncDir)
{
  VmsIo *io = alloc32(sizeof *io);
  unsigned st;
  int n, rc;
  (void) vfs; (void) syncDir;
  if (!io) return SQLITE_NOMEM;
  sqlite3_mutex_enter(vfsMutex());
  rc = setName(io, zName);
  sqlite3_mutex_leave(vfsMutex());
  if (rc) { free(io); return SQLITE_IOERR_DELETE; }
  /* every version: an old one would come back as the file */
  for (n = 0; n < 1000; n++) {
    initFab(io);
    st = sys$erase(&io->fab);
    if (!(st & 1)) break;
  }
  free(io);
  if (n > 0) return SQLITE_OK;
  if (st == RMS$_FNF) return SQLITE_IOERR_DELETE_NOENT;
  return vmsError(0, SQLITE_IOERR_DELETE, st, zName);
}

static int vmsAccess(sqlite3_vfs *vfs, const char *zName, int flags, int *pResOut)
{
  VmsIo *io = alloc32(sizeof *io);
  unsigned st;
  int rc;
  (void) vfs;
  *pResOut = 0;
  if (!io) return SQLITE_NOMEM;
  sqlite3_mutex_enter(vfsMutex());
  rc = setName(io, zName);
  sqlite3_mutex_leave(vfsMutex());
  if (rc) { free(io); return SQLITE_OK; }
  initFab(io);
  io->fab.fab$l_fop = FAB$M_UFO;
  io->fab.fab$b_fac = FAB$M_GET | FAB$M_BIO | (flags == SQLITE_ACCESS_READWRITE ? FAB$M_PUT : 0);
  io->fab.fab$b_shr = FAB$M_SHRGET | FAB$M_SHRPUT | FAB$M_UPI;
  st = sys$open(&io->fab);
  if (st & 1) {
    VmsFile f;
    memset(&f, 0, sizeof f);
    f.io = io;
    f.chan = (unsigned short) io->fab.fab$l_stv;
    memcpy(f.fid, io->naml.naml$w_fid, sizeof f.fid);
    /* as the unix VFS: an empty file does not "exist" (journals) */
    if (flags == SQLITE_ACCESS_EXISTS) *pResOut = (readAttr(&f) & 1) && f.eof > 0;
    else *pResOut = 1;
    sys$dassgn(f.chan);
  } else if (flags == SQLITE_ACCESS_EXISTS && st != RMS$_FNF && st != RMS$_DNF) {
    *pResOut = 1;       /* there, but we may not open it */
  }
  free(io);
  return SQLITE_OK;
}

static int vmsFullPathname(sqlite3_vfs *vfs, const char *zName, int nOut, char *zOut)
{
  VmsIo *io = alloc32(sizeof *io);
  unsigned st;
  int rc;
  (void) vfs;
  if (!io) return SQLITE_NOMEM;
  sqlite3_mutex_enter(vfsMutex());
  rc = setName(io, zName);
  sqlite3_mutex_leave(vfsMutex());
  if (rc) { free(io); return SQLITE_CANTOPEN; }
  initFab(io);
  st = sys$parse(&io->fab);
  if (!(st & 1)) { free(io); return vmsError(0, SQLITE_CANTOPEN, st, zName); }
  nameNoVersion(io, 0, zOut, nOut);
  free(io);
  return SQLITE_OK;
}

static int vmsRandomness(sqlite3_vfs *vfs, int n, char *zOut)
{
  unsigned __int64 t;
  int i;
  (void) vfs;
  sys$gettim((void *) &t);
  randomState ^= (unsigned) t ^ (unsigned) (t >> 32) ^ ((unsigned) getpid() << 16) ^
                 (unsigned) (unsigned long) zOut;
  for (i = 0; i < n; i++) {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    zOut[i] = (char) (randomState >> 11);
  }
  return n;
}

static int vmsSleep(sqlite3_vfs *vfs, int us)
{
  (void) vfs;
  if (us >= 1000000) sleep(us / 1000000);
  if (us % 1000000) usleep(us % 1000000);
  return us;
}

static int vmsCurrentTimeInt64(sqlite3_vfs *vfs, i64 *piNow)
{
  struct timeval tv;
  (void) vfs;
  gettimeofday(&tv, 0);
  /* Julian day in milliseconds; 24405875 * 8640000 is the Unix epoch */
  *piNow = (i64) 24405875 * 8640000 + (i64) (unsigned) tv.tv_sec * 1000 + tv.tv_usec / 1000;
  return SQLITE_OK;
}

static int vmsCurrentTime(sqlite3_vfs *vfs, double *prNow)
{
  i64 t;
  vmsCurrentTimeInt64(vfs, &t);
  *prNow = t / 86400000.0;
  return SQLITE_OK;
}

static int vmsGetLastError(sqlite3_vfs *vfs, int n, char *z)
{
  (void) vfs; (void) n; (void) z;
  return 0;
}

static sqlite3_vfs vmsVfs = {
  2,                    /* iVersion */
  sizeof(VmsFile),
  VMS_MAXPATH,
  0,
  "vms",
  0,
  vmsOpen, vmsDelete, vmsAccess, vmsFullPathname,
  0, 0, 0, 0,           /* no loadable extensions (D4) */
  vmsRandomness, vmsSleep, vmsCurrentTime, vmsGetLastError,
  vmsCurrentTimeInt64
};

/* At image exit, write what the process has written, as the OS cache would
** keep it on Unix.  No mutex: other threads may be gone mid-call. */
static void vmsAtExit(void)
{
  VmsFile *p;
  for (p = openFiles; p; p = p->pNext) {
    trace("exit: flush %s (eof %s)", p->zPath ? p->zPath : "temp", p->eofDirty ? "dirty" : "clean");
    if (p->chan && flushWbuf(p) == SQLITE_OK) flushEof(p);
  }
}

int sqlite3_os_init(void)
{
  static int exitHandler;
  if (!exitHandler) {
    const char *t = getenv("VMSSQLITE_TRACE");
    exitHandler = 1;
    atexit(vmsAtExit);
    if (t && *t) {
      char name[300];
      snprintf(name, sizeof name, "%sVMSSQLITE_%08X.LOG", t, (unsigned) getpid());
      traceFile = fopen(name, "w");
      trace("trace start");
    }
  }
  return sqlite3_vfs_register(&vmsVfs, 1);
}

int sqlite3_os_end(void)
{
  return SQLITE_OK;
}
