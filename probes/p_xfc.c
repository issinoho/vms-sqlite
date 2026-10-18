/* p_xfc.c - when does XFC cache $QIO reads of a file?  The node's volumes are
   in "VIOC Compatible mode" (SHOW MEMORY/CACHE), where XFC is said not to
   cache files open for write by more than one accessor - and SQLite's
   processes each open the database for write.

   "p_xfc a" and "p_xfc b" run together (vms_probe.com).  A creates a 4 MB
   file, then times 2 passes of 8192 one-block reads (wall clock) with:
     X1  A's channel read-only, no other accessor
     X2  A's channel read/write, no other accessor
     X3  A read-only while B holds the file open read/write
     X4  A read/write while B holds it open read/write (SQLite's case)
     X5  A read-only while B holds it open read-only */
#include <descrip.h>
#include <efndef.h>
#include <iodef.h>
#include <rms.h>
#include <starlet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static char role;
static struct FAB fab;
static char blk[4096];

static void step(int n) { char f[32]; FILE *fp; sprintf(f, "x_%d.step", n); fp = fopen(f, "w"); if (fp) fclose(fp); }
static void await(int n)
{
  char f[32]; int i;
  sprintf(f, "x_%d.step", n);
  for (i = 0; i < 1200 && access(f, 0) != 0; i++) usleep(100000);
}

static unsigned short openf(int write, int create)
{
  int st;
  fab = cc$rms_fab;
  fab.fab$l_fna = "xfc_test.db"; fab.fab$b_fns = 11;
  fab.fab$b_fac = FAB$M_GET | FAB$M_BIO | (write ? FAB$M_PUT : 0);
  fab.fab$b_shr = FAB$M_SHRGET | FAB$M_SHRPUT | FAB$M_UPI;
  fab.fab$l_fop = FAB$M_UFO | (create ? FAB$M_CIF : 0);
  fab.fab$l_alq = create ? 8200 : 0;
  st = create ? sys$create(&fab) : sys$open(&fab);
  if (!(st & 1)) { printf("%c: open %%X%08X\n", role, st); exit(1); }
  return (unsigned short) fab.fab$l_stv;
}

static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec + t.tv_usec / 1e6; }

static void timed(const char *tag, unsigned short chan)
{
  int pass, i;
  unsigned short iosb[4];
  for (pass = 1; pass <= 2; pass++) {
    double t0 = now();
    for (i = 0; i < 8192; i++)
      sys$qiow(EFN$C_ENF, chan, IO$_READVBLK, (void *) iosb, 0, 0, blk, 512, (i * 7919) % 8192 + 1, 0, 0, 0);
    printf("XFC %-44s pass %d: %6.0f reads/s\n", tag, pass, 8192 / (now() - t0));
  }
}

int main(int argc, char **argv)
{
  unsigned short c;
  int i;
  unsigned short iosb[4];
  role = argc > 1 ? argv[1][0] : '?';
  setvbuf(stdout, 0, _IONBF, 0);
  if (role == 'a') {
    c = openf(1, 1);
    memset(blk, 'x', sizeof blk);
    for (i = 0; i < 1024; i++)
      sys$qiow(EFN$C_ENF, c, IO$_WRITEVBLK, (void *) iosb, 0, 0, blk, 4096, i * 8 + 1, 0, 0, 0);
    sys$dassgn(c);
    c = openf(0, 0); timed("X1 read-only, alone", c); sys$dassgn(c);
    c = openf(1, 0); timed("X2 read/write, alone", c); sys$dassgn(c);
    step(1); await(2);                          /* B has it open read/write */
    c = openf(0, 0); timed("X3 read-only, B has it read/write", c); sys$dassgn(c);
    c = openf(1, 0); timed("X4 read/write, B has it read/write", c); sys$dassgn(c);
    step(3); await(4);                          /* B has it open read-only */
    c = openf(0, 0); timed("X5 read-only, B has it read-only", c); sys$dassgn(c);
    step(5);
  } else {
    await(1);
    c = openf(1, 0); step(2);
    await(3);
    sys$dassgn(c); c = openf(0, 0); step(4);
    await(5);
    sys$dassgn(c);
    usleep(2000000);
  }
  return 0;
}
