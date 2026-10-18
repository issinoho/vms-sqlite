/* p_lkns.c - which lock-manager namespace do the C RTL's fcntl() locks use?
   SQLite's users may run under different UIC groups (the PHP pool's account,
   the CLI's): user-mode locks without LCK$M_SYSTEM (SYSLCK) only meet other
   processes of the same group.  Takes an fcntl() read lock and a plain $ENQ
   lock of our own, then lists this process's locks with $GETLKI: resource
   name, namespace (system-wide or group, access mode) and modes. */
#include <descrip.h>
#include <fcntl.h>
#include <lckdef.h>
#include <lkidef.h>
#include <ssdef.h>
#include <starlet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* the 32-bit item list: addresses are longwords, so the buffers are static (P0) */
struct item { unsigned short len, code; unsigned addr, retlen; };
#define A32(p) ((unsigned) (unsigned long) (p))

int main(void)
{
  struct flock l;
  static unsigned short lksb[4], retlen[3];
  static unsigned lkid = 0, ns = 0, mode = 0;
  static char resnam[32];
  static struct item items[4];
  int fd, st, n = 0;
  $DESCRIPTOR(rn, "SQLITE_PROBE_OWN");

  fd = open("lkns_test.db", O_RDWR | O_CREAT, 0644);
  memset(&l, 0, sizeof l);
  l.l_type = F_RDLCK; l.l_whence = SEEK_SET; l.l_start = 0x40000002; l.l_len = 510;
  printf("LKNS fcntl F_RDLCK: %d\n", fcntl(fd, F_SETLK, &l));
  st = sys$enqw(0, LCK$K_PRMODE, (void *) lksb, 0, &rn, 0, 0, 0, 0, 0, 0, 0);
  printf("LKNS own $ENQ (user mode, no LCK$M_SYSTEM): %%X%08X / %%X%04X\n", st, lksb[0]);
  {
    static unsigned short sb[4];
    $DESCRIPTOR(rs, "SQLITE_PROBE_SYS");
    st = sys$enqw(0, LCK$K_PRMODE, (void *) sb, LCK$M_SYSTEM, &rs, 0, 0, 0, 0, 0, 0, 0);
    printf("LKNS own $ENQ with LCK$M_SYSTEM: %%X%08X / %%X%04X\n", st, sb[0]);
  }

  items[0].len = sizeof resnam; items[0].code = LKI$_RESNAM; items[0].addr = A32(resnam); items[0].retlen = A32(&retlen[0]);
  items[1].len = 4; items[1].code = LKI$_NAMSPACE; items[1].addr = A32(&ns); items[1].retlen = A32(&retlen[1]);
  items[2].len = 4; items[2].code = LKI$_STATE; items[2].addr = A32(&mode); items[2].retlen = A32(&retlen[2]);
  memset(&items[3], 0, sizeof items[3]);
  lkid = 0;    /* wildcard context: this process's locks */
  for (;;) {
    int i;
    memset(resnam, 0, sizeof resnam);
    st = sys$getlkiw(0, &lkid, items, (void *) lksb, 0, 0, 0);
    if (st == SS$_NOMORELOCK || !(st & 1)) break;
    printf("LKNS lock %08X: namespace %08X (%s, group %o, mode %u) state %08X name ", lkid, ns,
           (ns & LKI$M_SYSNAM) ? "SYSTEM-WIDE" : "group", (ns & 0xFFFF), (ns >> 16) & 0xFF, mode);
    for (i = 0; i < retlen[0]; i++)
      printf((resnam[i] >= 32 && resnam[i] < 127) ? "%c" : "<%02X>", (unsigned char) resnam[i]);
    printf("\n");
    if (++n > 300) break;
  }
  printf("LKNS end %%X%08X\n", st);
  close(fd); unlink("lkns_test.db");
  return 0;
}
