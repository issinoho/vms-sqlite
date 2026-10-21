/* p_cif.c - does opening an existing file fail now and then when several
   processes open it at the same moment?  CONCURRENT CC lost a worker about
   one run in 20 to "unable to open database file" at its start, when four
   workers and a reader open conc.db at once (PORTING_LOG #26).  os_vms.c
   opens a database it may create with RMS $CREATE and FAB$M_CIF (create
   if absent), UFO, shared GET/PUT/UPI, then reads its attributes.
   "p_cif <id> <mode>": 300 times, open cif_test.db as mode says, read its
   record attributes (ACP), deassign; count failures by status; then write
   done_<id>.txt.  Mode C: $CREATE with CIF (as os_vms.c), O: $OPEN.
   Mode W: a writer, as os_vms.c's: open once, then until done_1..5 exist,
   read the attributes and write a new end of file (IO$_MODIFY), and every
   tenth time extend the allocation.  Mode A: the C RTL's access() and
   stat() of the file, as the shell (and PHP's file_exists) do, until
   done_1..5 exist.  Mode J: create cif_test.db-journal ($CREATE, as
   os_vms.c), write a block, close and erase it, as SQLite's writers do at
   every transaction, until done_1..5 exist.  Several run at once
   (vms_probe.com), on a file that exists. */
#include <sys/stat.h>
#include <unistd.h>
#include <atrdef.h>
#include <descrip.h>
#include <efndef.h>
#include <fatdef.h>
#include <fibdef.h>
#include <iodef.h>
#include <rms.h>
#include <starlet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__clang__) || (defined(__INITIAL_POINTER_SIZE) && __INITIAL_POINTER_SIZE == 64)
#  define m32 _malloc32
#else
#  define m32 malloc
#endif

typedef struct Io {
  struct FAB fab; struct NAML naml; struct fibdef fib; struct dsc$descriptor fibd;
  struct atrdef atr[2]; FAT fat; unsigned short iosb[4];
  char name[256], esa[256], rsa[256];
} Io;

int main(int argc, char **argv)
{
  Io *io = m32(sizeof *io);
  int id = argc > 1 ? atoi(argv[1]) : 1, create = !(argc > 2 && argv[2][0] == 'O');
  unsigned st, bad[8] = { 0 }, nbad[8] = { 0 };
  int i, k, nb = 0, nfail = 0;
  if (argc > 2 && argv[2][0] == 'A') {
    struct stat sb;
    int n = 0;
    while (access("done_1.txt", 0) || access("done_2.txt", 0) || access("done_3.txt", 0) ||
           access("done_4.txt", 0) || access("done_5.txt", 0)) {
      access("cif_test.db", F_OK);
      stat("cif_test.db", &sb);
      n++;
    }
    printf("CIF access/stat %d: %d rounds\n", id, n);
    { char f[32]; FILE *fp; sprintf(f, "done_%d.txt", id); fp = fopen(f, "w"); if (fp) fclose(fp); }
    return 0;
  }
  if (argc > 2 && argv[2][0] == 'J') {
    static char blk[512];
    unsigned short iosb[4];
    int n = 0, jfail = 0;
    while (access("done_1.txt", 0) || access("done_2.txt", 0) || access("done_3.txt", 0) ||
           access("done_4.txt", 0) || access("done_5.txt", 0)) {
      memset(io, 0, sizeof *io);
      io->fab = cc$rms_fab; io->naml = cc$rms_naml;
      sprintf(io->name, "cif_test_%d.db-journal", id);
      io->fab.fab$l_fna = (char *) -1;
      io->fab.fab$l_naml = &io->naml;
      io->naml.naml$l_long_filename = io->name; io->naml.naml$l_long_filename_size = strlen(io->name);
      io->fab.fab$l_fop = FAB$M_UFO | FAB$M_CIF;
      io->fab.fab$b_fac = FAB$M_GET | FAB$M_BIO | FAB$M_PUT;
      io->fab.fab$b_shr = FAB$M_SHRGET | FAB$M_SHRPUT | FAB$M_UPI;
      io->fab.fab$b_rfm = FAB$C_STMLF;
      st = sys$create(&io->fab);
      if (st & 1) {
        sys$qiow(EFN$C_ENF, (unsigned short) io->fab.fab$l_stv, IO$_WRITEVBLK, (struct _iosb *) iosb, 0, 0,
                 blk, 512, 1, 0, 0, 0);
        sys$dassgn((unsigned short) io->fab.fab$l_stv);
        io->fab = cc$rms_fab; io->fab.fab$l_fna = (char *) -1; io->fab.fab$l_naml = &io->naml;
        st = sys$erase(&io->fab);
      }
      if (!(st & 1) && jfail++ < 3) printf("CIF journals %d: %%X%08X\n", id, st);
      n++;
    }
    printf("CIF journals %d: %d created and erased, %d failed\n", id, n, jfail);
    { char f[32]; FILE *fp; sprintf(f, "done_%d.txt", id); fp = fopen(f, "w"); if (fp) fclose(fp); }
    return 0;
  }
  if (argc > 2 && argv[2][0] == 'W') {
    unsigned short ch;
    int n = 0, wfail = 0;
    memset(io, 0, sizeof *io);
    io->fab = cc$rms_fab; io->naml = cc$rms_naml;
    strcpy(io->name, "cif_test.db");
    io->fab.fab$l_fna = (char *) -1;
    io->fab.fab$l_naml = &io->naml;
    io->naml.naml$l_long_filename = io->name; io->naml.naml$l_long_filename_size = strlen(io->name);
    io->fab.fab$l_fop = FAB$M_UFO;
    io->fab.fab$b_fac = FAB$M_GET | FAB$M_BIO | FAB$M_PUT;
    io->fab.fab$b_shr = FAB$M_SHRGET | FAB$M_SHRPUT | FAB$M_UPI;
    st = sys$open(&io->fab);
    if (!(st & 1)) { printf("CIF writer: open %%X%08X\n", st); return 1; }
    ch = (unsigned short) io->fab.fab$l_stv;
    while (access("done_1.txt", 0) || access("done_2.txt", 0) || access("done_3.txt", 0) ||
           access("done_4.txt", 0) || access("done_5.txt", 0)) {
      int pass;
      for (pass = 0; pass < 3; pass++) {
        unsigned func = pass == 0 ? IO$_ACCESS : IO$_MODIFY;
        memset(&io->fib, 0, sizeof io->fib);
        memcpy(&io->fib.fib$w_fid, io->naml.naml$w_fid, sizeof io->naml.naml$w_fid);
        if (pass == 2) {
          if (n % 10) break;
          io->fib.fib$w_exctl = FIB$M_EXTEND; io->fib.fib$l_exsz = 1;
        }
        io->fibd.dsc$w_length = sizeof io->fib; io->fibd.dsc$a_pointer = (char *) &io->fib;
        io->atr[0].atr$w_size = ATR$S_RECATTR; io->atr[0].atr$w_type = ATR$C_RECATTR;
        io->atr[0].atr$l_addr = &io->fat;
        memset(&io->atr[1], 0, sizeof io->atr[1]);
        if (pass == 1) { io->fat.fat$w_efblkl = (unsigned short) (2 + n % 50); io->fat.fat$w_efblkh = 0; io->fat.fat$w_ffbyte = 0; }
        st = sys$qiow(EFN$C_ENF, ch, func, (struct _iosb *) io->iosb, 0, 0, &io->fibd, 0, 0, 0,
                      pass == 2 ? 0 : (__int64) io->atr, 0);
        if (st & 1) st = io->iosb[0];
        if (!(st & 1) && wfail++ < 3) printf("CIF writer: pass %d %%X%08X\n", pass, st);
      }
      n++;
    }
    sys$dassgn(ch);
    printf("CIF writer: %d header updates, %d failed\n", n, wfail);
    { FILE *fp = fopen("done_7.txt", "w"); if (fp) fclose(fp); }
    return 0;
  }
  for (i = 0; i < 300; i++) {
    const char *where = "open";
    unsigned short ch;
    memset(io, 0, sizeof *io);
    io->fab = cc$rms_fab; io->naml = cc$rms_naml;
    strcpy(io->name, "cif_test.db");
    io->fab.fab$l_fna = (char *) -1; io->fab.fab$b_fns = 0;
    io->fab.fab$l_dna = "SYS$DISK:[]"; io->fab.fab$b_dns = 11;
    io->fab.fab$l_naml = &io->naml;
    io->naml.naml$l_long_filename = io->name; io->naml.naml$l_long_filename_size = strlen(io->name);
    io->naml.naml$l_long_expand = io->esa; io->naml.naml$l_long_expand_alloc = 255;
    io->naml.naml$l_long_result = io->rsa; io->naml.naml$l_long_result_alloc = 255;
    io->fab.fab$l_fop = FAB$M_UFO | (create ? FAB$M_CIF : 0);
    io->fab.fab$b_fac = FAB$M_GET | FAB$M_BIO | FAB$M_PUT;
    io->fab.fab$b_shr = FAB$M_SHRGET | FAB$M_SHRPUT | FAB$M_UPI;
    io->fab.fab$b_rfm = FAB$C_STMLF;
    io->fab.fab$b_rat = FAB$M_CR;
    st = create ? sys$create(&io->fab) : sys$open(&io->fab);
    if (st & 1) {
      ch = (unsigned short) io->fab.fab$l_stv;
      memcpy(&io->fib.fib$w_fid, io->naml.naml$w_fid, sizeof io->naml.naml$w_fid);
      io->fibd.dsc$w_length = sizeof io->fib; io->fibd.dsc$a_pointer = (char *) &io->fib;
      io->atr[0].atr$w_size = ATR$S_RECATTR; io->atr[0].atr$w_type = ATR$C_RECATTR;
      io->atr[0].atr$l_addr = &io->fat;
      st = sys$qiow(EFN$C_ENF, ch, IO$_ACCESS, (struct _iosb *) io->iosb, 0, 0, &io->fibd, 0, 0, 0,
                    (__int64) io->atr, 0);
      if (st & 1) st = io->iosb[0];
      if (!(st & 1)) where = "attributes";
      sys$dassgn(ch);
    }
    if (!(st & 1)) {
      nfail++;
      for (k = 0; k < nb && bad[k] != st; k++) ;
      if (k == nb && nb < 8) { bad[nb] = st; nb++; }
      if (k < 8) nbad[k]++;
      if (nfail <= 3) printf("CIF %d %s: %s failed %%X%08X (stv %%X%08X) at %d\n", id,
                             create ? "$CREATE/CIF" : "$OPEN", where, st, io->fab.fab$l_stv, i);
    }
  }
  printf("CIF %d %s: %d of 300 failed", id, create ? "$CREATE/CIF" : "$OPEN", nfail);
  for (k = 0; k < nb; k++) printf(", %%X%08X x%u", bad[k], nbad[k]);
  printf("\n");
  {
    char f[32]; FILE *fp;
    sprintf(f, "done_%d.txt", id);
    fp = fopen(f, "w"); if (fp) fclose(fp);
  }
  return 0;
}
