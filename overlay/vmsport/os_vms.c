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

typedef struct VmsFile {
  sqlite3_file base;
  VmsIo *io;                        /* 32-bit block */
  unsigned short chan;
  unsigned short fid[3];
  int isReadonly;
  int eLock;                        /* this connection's lock */
  VmsLockFile *lf;                  /* set at the first xLock */
  i64 eof;                          /* end of file as this connection knows it */
  int eofDirty;                     /* eof not yet written to the header */
  unsigned hiblk;                   /* allocated blocks, as last seen */
  int chunk;                        /* extension, blocks */
  char *zDelete;                    /* delete this name at close */
  char *zPath;                      /* the full name (for the lock file) */
  unsigned lastStatus;              /* last failing VMS status */
} VmsFile;

static VmsLockFile *lockFiles;

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
    unsigned st = sys$qiow(EFN$C_ENF, p->chan, write ? IO$_WRITEVBLK : IO$_READVBLK,
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

static int vmsClose(sqlite3_file *id)
{
  VmsFile *p = (VmsFile *) id;
  int rc = flushEof(p);
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

static int vmsRead(sqlite3_file *id, void *buf, int amt, i64 off)
{
  VmsFile *p = (VmsFile *) id;
  i64 end = off + amt, first, last, have;
  char *dst = buf;
  unsigned st;

  if (end > p->eof && !p->eofDirty) {          /* the header may know better */
    st = readAttr(p);
    if (!(st & 1)) return vmsError(p, SQLITE_IOERR_READ, st, "read attributes");
  }
  have = p->eof - off;
  if (have <= 0) { memset(buf, 0, amt); return SQLITE_IOERR_SHORT_READ; }
  if (have > amt) have = amt;
  /* head: a partial first block */
  first = off - off % VMS_BLOCK;
  if (off != first || have < VMS_BLOCK) {
    i64 k = VMS_BLOCK - (off - first);
    if (k > have) k = have;
    st = blockIo(p, 0, p->io->edge, VMS_BLOCK, first);
    if (!(st & 1)) return vmsError(p, SQLITE_IOERR_READ, st, "read");
    memcpy(dst, p->io->edge + (off - first), k);
    dst += k; off += k; have -= k;
  }
  /* middle: whole blocks straight into the caller's buffer */
  last = have - have % VMS_BLOCK;
  if (last > 0) {
    st = blockIo(p, 0, dst, last, off);
    if (!(st & 1)) return vmsError(p, SQLITE_IOERR_READ, st, "read");
    dst += last; off += last; have -= last;
  }
  /* tail */
  if (have > 0) {
    st = blockIo(p, 0, p->io->edge, VMS_BLOCK, off);
    if (!(st & 1)) return vmsError(p, SQLITE_IOERR_READ, st, "read");
    memcpy(dst, p->io->edge, have);
    dst += have;
  }
  if (dst - (char *) buf < amt) {
    memset(dst, 0, amt - (dst - (char *) buf));
    return SQLITE_IOERR_SHORT_READ;
  }
  return SQLITE_OK;
}

/* Read-modify-write one partial block: n bytes of src at offset off. */
static unsigned editBlock(VmsFile *p, const char *src, int n, i64 off)
{
  i64 b = off - off % VMS_BLOCK;
  unsigned st = SS$_NORMAL;
  if (b < p->eof) st = blockIo(p, 0, p->io->edge, VMS_BLOCK, b);
  else memset(p->io->edge, 0, VMS_BLOCK);
  if (!(st & 1)) return st;
  if (b < p->eof && p->eof - b < VMS_BLOCK)    /* bytes past the end of file are zeros */
    memset(p->io->edge + (p->eof - b), 0, VMS_BLOCK - (p->eof - b));
  memcpy(p->io->edge + (off - b), src, n);
  return blockIo(p, 1, p->io->edge, VMS_BLOCK, b);
}

static int vmsWrite(sqlite3_file *id, const void *buf, int amt, i64 off)
{
  VmsFile *p = (VmsFile *) id;
  const char *src = buf;
  i64 end = off + amt, mid;
  unsigned st;
  int rc;

  if (!p->eofDirty && off > p->eof) {          /* writing past our end: refresh it */
    st = readAttr(p);
    if (!(st & 1)) return vmsError(p, SQLITE_IOERR_WRITE, st, "read attributes");
  }
  rc = allocate(p, (unsigned) ((end + VMS_BLOCK - 1) / VMS_BLOCK));
  if (rc) return rc;
  if (off > p->eof) {
    /* a gap past the end: newly allocated blocks hold whatever was on the
    ** disk, so write zeros up to off */
    static const char z[VMS_XFER];
    i64 g = p->eof, gend = off - off % VMS_BLOCK;
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
    if (g > p->eof) p->eof = g;                 /* editBlock below sees zeros past here */
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
  if (end > p->eof) { p->eof = end; p->eofDirty = 1; }
  return SQLITE_OK;
}

static int vmsTruncate(sqlite3_file *id, i64 size)
{
  VmsFile *p = (VmsFile *) id;
  unsigned st, keep;
  int rc;
  if (size > p->eof) {                          /* growing: allocate, the blocks are zeroed */
    rc = vmsWrite(id, "", 0, size);
    if (rc) return rc;
  }
  p->eof = size;
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
  (void) flags;
  return flushEof((VmsFile *) id);
}

static int vmsFileSize(sqlite3_file *id, i64 *pSize)
{
  VmsFile *p = (VmsFile *) id;
  if (!p->eofDirty) {
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

static int vmsLock(sqlite3_file *id, int eLock)
{
  VmsFile *p = (VmsFile *) id;
  VmsLockFile *lf;
  int rc = SQLITE_OK;

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
  p->eLock = lf->eLock = EXCLUSIVE_LOCK;
  goto out;

refreshed:
  /* a new read transaction: the file may have changed size */
  if (!p->eofDirty) {
    unsigned st = readAttr(p);
    if (!(st & 1)) rc = vmsError(p, SQLITE_IOERR_RDLOCK, st, "read attributes");
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
  if (p->eLock > SHARED_LOCK) rc = flushEof(p);   /* before others can look */
  sqlite3_mutex_enter(vfsMutex());
  if (p->eLock > SHARED_LOCK) {
    if (eLock == SHARED_LOCK && setShared(lf, F_RDLCK)) rc = SQLITE_IOERR_RDLOCK;
    if (lf->pending) { lk(lf, F_UNLCK, PENDING_BYTE, 1); lf->pending = 0; }
    if (lf->reserved) { lk(lf, F_UNLCK, RESERVED_BYTE, 1); lf->reserved = 0; }
    lf->eLock = SHARED_LOCK;
  }
  if (eLock == NO_LOCK) {
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
  return vmsLockRelease((VmsFile *) id, eLock);
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

int sqlite3_os_init(void)
{
  return sqlite3_vfs_register(&vmsVfs, 1);
}

int sqlite3_os_end(void)
{
  return SQLITE_OK;
}
