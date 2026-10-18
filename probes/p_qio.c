/* p_qio.c - can a VMS VFS do coherent file I/O between processes with $QIO?
   (p_mp showed the C RTL can't: each descriptor keeps its own buffers.)
   Files are opened by RMS with UFO (we get the channel, RMS does no I/O),
   shared GET/PUT/UPI; data moves with IO$_READVBLK/IO$_WRITEVBLK; the end of
   file is read and written as the file header's record attributes (FAT).
   Run "p_qio a" and "p_qio b" together (vms_probe.com).

   Q1  B's write (read-modify-write of block 1) seen by A, which wrote it before
   Q2  B extends the file (IO$_MODIFY extend, write, set EOF); A sees size + data
   Q3  B shrinks the EOF to 4096; A sees it
   Q4  after both deassign, the C RTL's stat() agrees on the size
   Q5  timing: 4 KB reads and writes per second
   Q6  B also keeps a C RTL descriptor (O_RDWR) open for fcntl() locks only,
       from before the EOF changes; does closing it write a stale EOF back? */
#include <errno.h>
#include <fcntl.h>
#include <atrdef.h>
#include <descrip.h>
#include <fatdef.h>
#include <fibdef.h>
#include <iodef.h>
#include <rms.h>
#include <ssdef.h>
#include <starlet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char role;
static struct FAB fab;
static struct NAM nam;
static unsigned short chan, fid[3];
static char blk[4096 + 512];
static unsigned short iosb[4];
static int cfd = -1;

static void step(int n) { char f[32]; FILE *fp; sprintf(f, "q_%d.step", n); fp = fopen(f, "w"); if (fp) fclose(fp); }
static void await(int n)
{
  char f[32]; int i;
  sprintf(f, "q_%d.step", n);
  for (i = 0; i < 600 && access(f, 0) != 0; i++) usleep(100000);
  if (i == 600) { printf("%c: timeout at %d\n", role, n); exit(1); }
}
static void check(const char *what, int ok, const char *d) { printf("%c %-4s %-44s %s\n", role, ok ? "OK" : "BAD", what, d); }

static int openf(const char *name, int create)
{
  int st;
  fab = cc$rms_fab; nam = cc$rms_nam;
  fab.fab$l_fna = (char *) name; fab.fab$b_fns = strlen(name);
  fab.fab$b_fac = FAB$M_GET | FAB$M_PUT | FAB$M_BIO;
  fab.fab$b_shr = FAB$M_SHRGET | FAB$M_SHRPUT | FAB$M_UPI;
  fab.fab$l_fop = FAB$M_UFO | (create ? FAB$M_CIF : 0);
  fab.fab$b_rfm = FAB$C_UDF;
  fab.fab$l_nam = &nam;
  st = create ? sys$create(&fab) : sys$open(&fab);
  if (!(st & 1)) { printf("%c: open %s %%X%08X\n", role, name, st); return st; }
  chan = (unsigned short) fab.fab$l_stv;
  memcpy(fid, nam.nam$w_fid, sizeof fid);
  return st;
}

/* the file header: read or write its record attributes */
static int attr(int write, FAT *fat)
{
  struct fibdef fib; struct dsc$descriptor fibd;
  struct atrdef atr[2];
  int st;
  memset(&fib, 0, sizeof fib);
  memcpy(&fib.fib$w_fid, fid, sizeof fid);
  fibd.dsc$w_length = sizeof fib; fibd.dsc$a_pointer = (char *) &fib;
  fibd.dsc$b_dtype = 0; fibd.dsc$b_class = 0;
  atr[0].atr$w_size = ATR$S_RECATTR; atr[0].atr$w_type = ATR$C_RECATTR;
  atr[0].atr$l_addr = fat;
  memset(&atr[1], 0, sizeof atr[1]);
  st = sys$qiow(0, chan, write ? IO$_MODIFY : IO$_ACCESS, (void *) iosb, 0, 0,
                &fibd, 0, 0, 0, (__int64) atr, 0);
  if (st & 1) st = iosb[0];
  return st;
}

static long getsize(void)
{
  FAT fat;
  int st = attr(0, &fat);
  if (!(st & 1)) { printf("%c: read attributes %%X%08X\n", role, st); return -1; }
  return ((long) ((fat.fat$w_efblkh << 16) | fat.fat$w_efblkl) - 1) * 512 + fat.fat$w_ffbyte;
}

static int setsize(long size)
{
  FAT fat;
  unsigned eb = size / 512 + 1;
  int st = attr(0, &fat);
  if (!(st & 1)) return st;
  fat.fat$w_efblkh = eb >> 16; fat.fat$w_efblkl = eb & 0xFFFF;
  fat.fat$w_ffbyte = size % 512;
  return attr(1, &fat);
}

static int extend(unsigned blocks)          /* allocate at least this many blocks */
{
  struct fibdef fib; struct dsc$descriptor fibd;
  int st;
  memset(&fib, 0, sizeof fib);
  memcpy(&fib.fib$w_fid, fid, sizeof fid);
  fib.fib$w_exctl = FIB$M_EXTEND;
  fib.fib$l_exsz = blocks;
  fibd.dsc$w_length = sizeof fib; fibd.dsc$a_pointer = (char *) &fib;
  fibd.dsc$b_dtype = 0; fibd.dsc$b_class = 0;
  st = sys$qiow(0, chan, IO$_MODIFY, (void *) iosb, 0, 0, &fibd, 0, 0, 0, 0, 0);
  return (st & 1) ? iosb[0] : st;
}

static int rd(void *buf, int len, long off)   /* off and len: whole blocks */
{
  int st = sys$qiow(0, chan, IO$_READVBLK, (void *) iosb, 0, 0, buf, len, off / 512 + 1, 0, 0, 0);
  return (st & 1) ? iosb[0] : st;
}
static int wr(void *buf, int len, long off)
{
  int st = sys$qiow(0, chan, IO$_WRITEVBLK, (void *) iosb, 0, 0, buf, len, off / 512 + 1, 0, 0, 0);
  return (st & 1) ? iosb[0] : st;
}
static int poke(long off, char c)             /* one byte: read-modify-write its block */
{
  long b = off & ~511L; int st = rd(blk, 512, b);
  if (!(st & 1)) return st;
  blk[off - b] = c;
  return wr(blk, 512, b);
}
static char peek(long off) { long b = off & ~511L; return (rd(blk, 512, b) & 1) ? blk[off - b] : '?'; }

int main(int argc, char **argv)
{
  char d[100]; long sz; int st, i;
  role = argc > 1 ? argv[1][0] : '?';
  setvbuf(stdout, 0, _IONBF, 0);
  if (role == 'a') {
    if (!(openf("qio_test.db", 1) & 1)) return 1;
    st = extend(16);
    sprintf(d, "%%X%08X", st); check("Q0 extend to 16 blocks", st & 1, d);
    memset(blk, 'A', 4096); wr(blk, 4096, 0); wr(blk, 4096, 4096);
    st = setsize(8192); sz = getsize();
    sprintf(d, "set %%X%08X, read back %ld", st, sz); check("Q0 set EOF 8192", sz == 8192, d);
    poke(100, 'a');
    step(1); await(2);
    sprintf(d, "byte 100 '%c' (B wrote 'b')", peek(100)); check("Q1 see the other process's write", peek(100) == 'b', d);
    step(3); await(4);
    sz = getsize();
    sprintf(d, "size %ld, byte 8197 '%c'", sz, peek(8197));
    check("Q2 size + data after the other's extend", sz == 12288 && peek(8197) == 'B', d);
    step(5); await(6);
    sz = getsize(); sprintf(d, "size %ld", sz); check("Q3 size after the other's shrink", sz == 4096, d);
    {
      clock_t t0 = clock(); int n = 5000;
      for (i = 0; i < n; i++) rd(blk, 4096, 0);
      sprintf(d, "%.0f reads/s", n / ((clock() - t0) / (double) CLOCKS_PER_SEC + 1e-9));
      check("Q5 4 KB reads", 1, d);
      t0 = clock();
      for (i = 0; i < 1000; i++) wr(blk, 4096, 0);
      sprintf(d, "%.0f writes/s", 1000 / ((clock() - t0) / (double) CLOCKS_PER_SEC + 1e-9));
      check("Q5 4 KB writes", 1, d);
    }
    st = sys$dassgn(chan); printf("a: dassgn %%X%08X\n", st);
    step(7); await(8); printf("a: step 8 seen\n");
    {
      struct stat s; stat("qio_test.db", &s);
      sprintf(d, "stat() %ld", (long) s.st_size); check("Q4/Q6 size after both close (8192 = stale EOF written)", s.st_size == 4096, d);
      fflush(stdout);
    }
  } else {
    await(1);
    if (!(openf("qio_test.db", 0) & 1)) { step(2); step(4); step(6); step(8); return 1; }
    {
      struct flock l;
      cfd = getenv("Q6_FACGET") ? open("qio_test.db", O_RDWR, 0, "fac=get", "shr=get,put,upd") : open("qio_test.db", O_RDWR);
      memset(&l, 0, sizeof l); l.l_whence = SEEK_SET; l.l_start = 0x40000000; l.l_len = 1;
      l.l_type = F_WRLCK; st = fcntl(cfd, F_SETLK, &l);
      l.l_type = F_UNLCK; fcntl(cfd, F_SETLK, &l);
      sprintf(d, "fd %d, lock rc %d errno %d%s", cfd, st, st ? errno : 0, getenv("Q6_FACGET") ? " (fac=get)" : ""); check("Q6 C RTL descriptor for locks", cfd >= 0 && st == 0, d);
    }
    sz = getsize(); sprintf(d, "size %ld", sz); check("Q0 second process sees EOF", sz == 8192, d);
    sprintf(d, "byte 100 '%c' (A wrote 'a')", peek(100)); check("Q1 see the other process's write", peek(100) == 'a', d);
    poke(100, 'b');
    step(2); await(3);
    st = extend(8);
    memset(blk, 'B', 4096); wr(blk, 4096, 8192);
    st = setsize(12288);
    sprintf(d, "%%X%08X", st); check("Q2 extend + set EOF 12288", st & 1, d);
    step(4); await(5);
    st = setsize(4096); sprintf(d, "%%X%08X", st); check("Q3 set EOF 4096", st & 1, d);
    step(6); await(7);
    if (cfd >= 0) close(cfd);     /* Q6: the lock descriptor goes first */
    sys$dassgn(chan);
    step(8);
    usleep(4000000);   /* A must finish before this procedure ends */
  }
  return 0;
}
