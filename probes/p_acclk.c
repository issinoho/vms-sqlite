/* p_acclk.c - can xAccess look a name up with one ACP call instead of an
   RMS open (D8 (2) (b))?  Names come as RMS specs, escaped on ODS-5
   ("acc^.two.db", "acc^_space.db"); the ACP looks up the directory entry.
   For each name, an RMS open is the reference; then:
     A  $PARSE, then IO$_ACCESS by name in naml$w_did, with the name
        unescaped by us and FIB$M_NAMES_8BIT, and the record attributes
        (end of file) in the same call
     B  the same with the name as $PARSE wrote it (escaped), no flag
     C  naml$l_filesys_name, if $PARSE fills it
   L1-L3: cost per call of the whole of A (parse, assign, ACP, deassign). */
#include <atrdef.h>
#include <descrip.h>
#include <efndef.h>
#include <fatdef.h>
#include <fibdef.h>
#include <iodef.h>
#include <rms.h>
#include <ssdef.h>
#include <starlet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#if defined(__clang__) || (defined(__INITIAL_POINTER_SIZE) && __INITIAL_POINTER_SIZE == 64)
#  define m32 _malloc32
#else
#  define m32 malloc
#endif

typedef struct Io {
  struct FAB fab; struct NAML naml; struct fibdef fib; struct dsc$descriptor fibd, nmd;
  struct atrdef atr[2]; FAT fat; unsigned short iosb[4];
  char name[4096], esa[4096], rsa[4096], fsn[4096], raw[4096];
} Io;

static Io *io;

static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec * 1e6 + t.tv_usec; }

static void setup(const char *name)
{
  memset(io, 0, sizeof *io);
  io->fab = cc$rms_fab; io->naml = cc$rms_naml;
  strcpy(io->name, name);
  io->fab.fab$l_fna = (char *) -1; io->fab.fab$b_fns = 0;
  io->fab.fab$l_dna = "SYS$DISK:[]"; io->fab.fab$b_dns = 11;
  io->fab.fab$l_naml = &io->naml;
  io->naml.naml$l_long_filename = io->name; io->naml.naml$l_long_filename_size = strlen(name);
  io->naml.naml$l_long_expand = io->esa; io->naml.naml$l_long_expand_alloc = 4095;
  io->naml.naml$l_long_result = io->rsa; io->naml.naml$l_long_result_alloc = 4095;
  io->naml.naml$l_filesys_name = io->fsn; io->naml.naml$l_filesys_name_alloc = 4095;
  io->fab.fab$l_fop = FAB$M_UFO;
  io->fab.fab$b_fac = FAB$M_GET | FAB$M_BIO;
  io->fab.fab$b_shr = FAB$M_SHRGET | FAB$M_SHRPUT | FAB$M_UPI;
}

static unsigned rmsOpen(const char *name, unsigned *eof)
{
  unsigned st;
  setup(name);
  io->fab.fab$b_rfm = FAB$C_STMLF;
  st = sys$open(&io->fab);
  *eof = 0;
  if (st & 1) sys$dassgn((unsigned short) io->fab.fab$l_stv);
  return st;
}

/* RMS escapes (^. ^_ ^XX ^c) removed: the name as the directory holds it */
static int unescape(const char *s, int n, char *out)
{
  int i, k = 0;
  for (i = 0; i < n; i++) {
    if (s[i] != '^' || i + 1 >= n) { out[k++] = s[i]; continue; }
    i++;
    if (s[i] == '_') out[k++] = ' ';
    else if (i + 1 < n && strchr("0123456789ABCDEFabcdef", s[i]) && strchr("0123456789ABCDEFabcdef", s[i + 1])) {
      char h[3] = { s[i], s[i + 1], 0 };
      out[k++] = (char) strtol(h, 0, 16); i++;
    } else out[k++] = s[i];
  }
  return k;
}

/* mode 'A' unescaped + NAMES_8BIT, 'B' escaped, 'C' filesys name */
static unsigned lookup(const char *name, int mode, unsigned *efblk)
{
  struct dsc$descriptor dev;
  unsigned short ch, iosb[4];
  unsigned st;
  char nbuf[600];
  int n;
  setup(name);
  st = sys$parse(&io->fab);
  if (!(st & 1)) return st;
  if (mode == 'C') {
    if (!io->naml.naml$l_filesys_name_size) return 0;
    memcpy(nbuf, io->fsn, n = io->naml.naml$l_filesys_name_size);
  } else if (mode == 'A') {
    n = unescape(io->naml.naml$l_long_name, io->naml.naml$l_long_name_size, nbuf);
    n += unescape(io->naml.naml$l_long_type, io->naml.naml$l_long_type_size, nbuf + n);
  } else {
    memcpy(nbuf, io->naml.naml$l_long_name, io->naml.naml$l_long_name_size);
    n = io->naml.naml$l_long_name_size;
    memcpy(nbuf + n, io->naml.naml$l_long_type, io->naml.naml$l_long_type_size);
    n += io->naml.naml$l_long_type_size;
  }
  strcpy(nbuf + n, ";0"); n += 2;
  strcpy(io->raw, nbuf);
  dev.dsc$w_length = io->naml.naml$l_long_dev_size; dev.dsc$a_pointer = io->naml.naml$l_long_dev;
  dev.dsc$b_dtype = DSC$K_DTYPE_T; dev.dsc$b_class = DSC$K_CLASS_S;
  st = sys$assign(&dev, &ch, 0, 0);
  if (!(st & 1)) return st;
  memcpy(&io->fib.fib$w_did, io->naml.naml$w_did, sizeof io->naml.naml$w_did);
  if (mode == 'A') io->fib.fib$w_nmctl = FIB$M_NAMES_8BIT;
  io->fibd.dsc$w_length = sizeof io->fib; io->fibd.dsc$a_pointer = (char *) &io->fib;
  io->nmd.dsc$w_length = n; io->nmd.dsc$a_pointer = io->raw;
  io->atr[0].atr$w_size = ATR$S_RECATTR; io->atr[0].atr$w_type = ATR$C_RECATTR;
  io->atr[0].atr$l_addr = &io->fat;
  st = sys$qiow(EFN$C_ENF, ch, IO$_ACCESS, (struct _iosb *) iosb, 0, 0, &io->fibd, &io->nmd, 0, 0,
                (__int64) io->atr, 0);
  if (st & 1) st = iosb[0];
  *efblk = ((unsigned) io->fat.fat$w_efblkh << 16) | io->fat.fat$w_efblkl;
  sys$dassgn(ch);
  return st;
}

static void make(const char *name, int nblk)
{
  static char blk[512];
  unsigned short iosb[4];
  int i;
  setup(name);
  io->fab.fab$b_fac = FAB$M_PUT | FAB$M_BIO;
  io->fab.fab$b_rfm = FAB$C_STMLF;
  if (!(sys$create(&io->fab) & 1)) { printf("create %s failed\n", name); return; }
  for (i = 0; i < nblk; i++)
    sys$qiow(EFN$C_ENF, (unsigned short) io->fab.fab$l_stv, IO$_WRITEVBLK, (struct _iosb *) iosb, 0, 0,
             blk, 512, i + 1, 0, 0, 0);
  sys$dassgn((unsigned short) io->fab.fab$l_stv);
}

int main(void)
{
  static const char *names[] = {
    "acc_plain.db", "acc^.two.db", "Acc_Mixed.DB", "acc_mixed.db", "acc^_space.db",
    "acc_empty.db", "acc_missing.db", "acc^.missing.db", 0
  };
  unsigned st, eof, ef;
  double t0;
  int i, m;

  io = m32(sizeof *io);
  make("acc_plain.db", 2);
  make("acc^.two.db", 1);
  make("Acc_Mixed.DB", 1);
  make("acc^_space.db", 1);
  make("acc_empty.db", 0);
  for (i = 0; names[i]; i++) {
    st = rmsOpen(names[i], &eof);
    printf("%-18s RMS %%X%08X\n", names[i], st);
    for (m = 'A'; m <= 'C'; m++) {
      st = lookup(names[i], m, &ef);
      printf("%-18s   %c %%X%08X efblk %u name [%s]\n", "", m, st, ef, io->raw);
    }
  }
  t0 = now();
  for (i = 0; i < 1000; i++) lookup("acc_missing.db", 'A', &ef);
  printf("L1   A, missing file         %8.1f us\n", (now() - t0) / 1000);
  t0 = now();
  for (i = 0; i < 1000; i++) lookup("acc_plain.db", 'A', &ef);
  printf("L2   A, existing file        %8.1f us\n", (now() - t0) / 1000);
  t0 = now();
  for (i = 0; i < 1000; i++) rmsOpen("acc_missing.db", &eof);
  printf("L3   RMS open, missing file  %8.1f us\n", (now() - t0) / 1000);
  return 0;
}
