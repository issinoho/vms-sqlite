/* p_cost.c - what does each primitive of an SQLite read transaction cost?
   speedtest1's tests 410/510 (17,500 autocommit SELECTs) take ~4.3 ms per
   transaction on VMS; per transaction os_vms.c does (PHASE2.md):
     xLock(SHARED)   fcntl: PENDING rdlock, SHARED rdlock, PENDING unlock;
                     ACP read attributes; $QIO read block 1 (change counter)
     hot journal     xAccess("-journal"): _malloc32 a VmsIo, sys$open -> FNF
     page count      xFileSize: ACP read attributes
     xUnlock         fcntl: SHARED unlock
   Each is timed alone over N iterations, in microseconds per call.

   C1  fcntl F_RDLCK + F_UNLCK of one byte (a pair)
   C2  ACP IO$_ACCESS, record attributes
   C3  $QIO IO$_READVBLK, one block
   C4  sys$open (UFO, NAML) of a file that does not exist
   C5  _malloc32 + memset + free of 17 KB (VmsIo)
   C6  sys$open + sys$dassgn of a file that exists
   C7  sys$parse of a name, no file access
   C8  ACP lookup by name in the directory (IO$_ACCESS, FIB did + name, on a
       channel assigned to the device): missing file, then existing file
   C9  sys$parse + sys$search, missing file
   C10 C RTL access(), missing file;  C11 stat(), missing file */
#include <atrdef.h>
#include <descrip.h>
#include <efndef.h>
#include <fatdef.h>
#include <fibdef.h>
#include <iodef.h>
#include <rms.h>
#include <ssdef.h>
#include <starlet.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define N 2000

#if defined(__clang__) || (defined(__INITIAL_POINTER_SIZE) && __INITIAL_POINTER_SIZE == 64)
#  define m32 _malloc32
#else
#  define m32 malloc
#endif

typedef struct Io {
  struct FAB fab; struct NAML naml; struct fibdef fib; struct dsc$descriptor fibd;
  struct atrdef atr[2]; FAT fat; unsigned short iosb[4];
  char name[4096], esa[4096], rsa[4096], blk[512];
} Io;

static Io *io;
static unsigned short chan, fid[3];

static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec * 1e6 + t.tv_usec; }
static void report(const char *id, const char *what, double t0, int n)
{
  printf("%-4s %-46s %8.1f us\n", id, what, (now() - t0) / n);
}

static unsigned rmsOpen(Io *o, const char *name, int parseOnly)
{
  o->fab = cc$rms_fab; o->naml = cc$rms_naml;
  strcpy(o->name, name);
  o->fab.fab$l_fna = (char *) -1; o->fab.fab$b_fns = 0;
  o->fab.fab$l_dna = "SYS$DISK:[]"; o->fab.fab$b_dns = 11;
  o->fab.fab$l_naml = &o->naml;
  o->naml.naml$l_long_filename = o->name; o->naml.naml$l_long_filename_size = strlen(o->name);
  o->naml.naml$l_long_expand = o->esa; o->naml.naml$l_long_expand_alloc = 4095;
  o->naml.naml$l_long_result = o->rsa; o->naml.naml$l_long_result_alloc = 4095;
  o->fab.fab$l_fop = FAB$M_UFO;
  o->fab.fab$b_fac = FAB$M_GET | FAB$M_BIO;
  o->fab.fab$b_shr = FAB$M_SHRGET | FAB$M_SHRPUT | FAB$M_UPI;
  return parseOnly ? sys$parse(&o->fab) : sys$open(&o->fab);
}

static unsigned readAttr(void)
{
  unsigned st;
  memset(&io->fib, 0, sizeof io->fib);
  memcpy(&io->fib.fib$w_fid, fid, sizeof fid);
  io->fibd.dsc$w_length = sizeof io->fib; io->fibd.dsc$a_pointer = (char *) &io->fib;
  io->fibd.dsc$b_dtype = DSC$K_DTYPE_Z; io->fibd.dsc$b_class = DSC$K_CLASS_S;
  io->atr[0].atr$w_size = ATR$S_RECATTR; io->atr[0].atr$w_type = ATR$C_RECATTR;
  io->atr[0].atr$l_addr = &io->fat;
  memset(&io->atr[1], 0, sizeof io->atr[1]);
  st = sys$qiow(EFN$C_ENF, chan, IO$_ACCESS, (struct _iosb *) io->iosb, 0, 0, &io->fibd, 0, 0, 0,
                (__int64) io->atr, 0);
  return (st & 1) ? io->iosb[0] : st;
}

int main(void)
{
  struct flock l;
  unsigned short iosb[4];
  unsigned st;
  double t0;
  int fd, i;
  FILE *f;

  io = m32(sizeof *io);
  memset(io, 0, sizeof *io);
  f = fopen("cost_test.db", "w");                 /* 64 KB of data */
  for (i = 0; i < 65536; i++) fputc(i & 0xFF, f);
  fclose(f);
  fd = open("cost_test.lck", O_RDWR | O_CREAT, 0, "shr=get,put,upd");
  if (fd < 0) { perror("open lock file"); return 1; }
  st = rmsOpen(io, "cost_test.db", 0);
  if (!(st & 1)) { printf("open %%X%08X\n", st); return 1; }
  chan = (unsigned short) io->fab.fab$l_stv;
  memcpy(fid, io->naml.naml$w_fid, sizeof fid);

  memset(&l, 0, sizeof l);
  l.l_whence = SEEK_SET; l.l_start = 4; l.l_len = 1;
  t0 = now();
  for (i = 0; i < N; i++) {
    l.l_type = F_RDLCK; if (fcntl(fd, F_SETLK, &l)) { perror("fcntl"); break; }
    l.l_type = F_UNLCK; fcntl(fd, F_SETLK, &l);
  }
  report("C1", "fcntl lock + unlock (pair)", t0, N);

  t0 = now();
  for (i = 0; i < N; i++) if (!((st = readAttr()) & 1)) { printf("acp %%X%08X\n", st); break; }
  report("C2", "ACP read attributes", t0, N);

  t0 = now();
  for (i = 0; i < N; i++) {
    st = sys$qiow(EFN$C_ENF, chan, IO$_READVBLK, (struct _iosb *) iosb, 0, 0, io->blk, 512, 1, 0, 0, 0);
    if (st & 1) st = iosb[0];
    if (!(st & 1)) { printf("qio %%X%08X\n", st); break; }
  }
  report("C3", "$QIO read one block", t0, N);

  t0 = now();
  for (i = 0; i < N; i++) {
    Io *o = m32(sizeof *o);
    memset(o, 0, sizeof *o);
    st = rmsOpen(o, "cost_test.db-journal", 0);
    free(o);
  }
  report("C4", "sys$open, no such file (incl. C5)", t0, N);
  printf("     status %%X%08X\n", st);

  t0 = now();
  for (i = 0; i < N; i++) { Io *o = m32(sizeof *o); memset(o, 0, sizeof *o); free(o); }
  report("C5", "_malloc32 + memset + free 17 KB", t0, N);

  t0 = now();
  for (i = 0; i < N / 4; i++) {
    Io *o = m32(sizeof *o);
    memset(o, 0, sizeof *o);
    st = rmsOpen(o, "cost_test.lck", 0);
    if (st & 1) sys$dassgn((unsigned short) o->fab.fab$l_stv);
    free(o);
  }
  report("C6", "sys$open + sys$dassgn, file exists", t0, N / 4);

  {
    Io *o = m32(sizeof *o);
    t0 = now();
    for (i = 0; i < N; i++) st = rmsOpen(o, "cost_test.db-journal", 1);
    report("C7", "sys$parse", t0, N);
    printf("     status %%X%08X\n", st);
    free(o);
  }

  {
    /* the directory and device of the database, from its open */
    struct dsc$descriptor dev, nm;
    struct fibdef fib;
    struct dsc$descriptor fibd;
    unsigned short dch, did[3];
    static const char *names[2] = { "cost_test.db-journal", "cost_test.lck" };
    int k;
    memcpy(did, io->naml.naml$w_did, sizeof did);
    dev.dsc$w_length = io->naml.naml$l_long_dev_size; dev.dsc$a_pointer = io->naml.naml$l_long_dev;
    dev.dsc$b_dtype = DSC$K_DTYPE_T; dev.dsc$b_class = DSC$K_CLASS_S;
    st = sys$assign(&dev, &dch, 0, 0);
    if (!(st & 1)) printf("assign %%X%08X\n", st);
    for (k = 0; k < 2; k++) {
      char nbuf[64];
      sprintf(nbuf, "%s;0", names[k]);
      nm.dsc$w_length = strlen(nbuf); nm.dsc$a_pointer = nbuf;
      nm.dsc$b_dtype = DSC$K_DTYPE_T; nm.dsc$b_class = DSC$K_CLASS_S;
      fibd.dsc$w_length = sizeof fib; fibd.dsc$a_pointer = (char *) &fib;
      fibd.dsc$b_dtype = DSC$K_DTYPE_Z; fibd.dsc$b_class = DSC$K_CLASS_S;
      t0 = now();
      for (i = 0; i < N; i++) {
        memset(&fib, 0, sizeof fib);
        memcpy(&fib.fib$w_did, did, sizeof did);
        st = sys$qiow(EFN$C_ENF, dch, IO$_ACCESS, (struct _iosb *) iosb, 0, 0, &fibd, &nm, 0, 0, 0, 0);
        if (st & 1) st = iosb[0];
      }
      report(k ? "C8b" : "C8a", k ? "ACP lookup, file exists" : "ACP lookup, no such file", t0, N);
      printf("     status %%X%08X fid %u,%u,%u\n", st, fib.fib$w_fid[0], fib.fib$w_fid[1], fib.fib$w_fid[2]);
    }
    sys$dassgn(dch);
  }

  {
    Io *o = m32(sizeof *o);
    unsigned st2 = 0;
    t0 = now();
    for (i = 0; i < N; i++) {
      memset(o, 0, sizeof *o);
      st = rmsOpen(o, "cost_test.db-journal", 1);
      if (st & 1) st2 = sys$search(&o->fab);
    }
    report("C9", "sys$parse + sys$search, no such file", t0, N);
    printf("     status %%X%08X %%X%08X\n", st, st2);
    free(o);
  }

  t0 = now();
  for (i = 0; i < N; i++) st = access("cost_test.db-journal", F_OK);
  report("C10", "access(), no such file", t0, N);
  {
    struct stat sb;
    t0 = now();
    for (i = 0; i < N; i++) st = stat("cost_test.db-journal", &sb);
    report("C11", "stat(), no such file", t0, N);
  }

  sys$dassgn(chan);
  close(fd);
  return 0;
}
