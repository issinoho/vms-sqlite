/* p_getlk.c - can xCheckReservedLock test the RESERVED byte with F_GETLK?
   os_vms.c tested it by taking a write lock and releasing it: while several
   processes test at once (each looking for a hot journal after a writer
   died), one's test lock makes another's test fail, which reads as "a live
   writer holds RESERVED", so that process takes the dead writer's journal
   for its own and overwrites it (mptest config02, PORTING_LOG #25).  F_GETLK
   tests without taking anything.
   Run "p_getlk a" and "p_getlk b" together (vms_probe.com).
     K1  B holds a write lock on byte 2: A's F_GETLK sees it (type, pid)
     K2  B released it: A's F_GETLK sees none (F_UNLCK)
     K3  A holds a read lock on byte 4 (as SHARED): its own F_GETLK on byte 2
         still sees none
     K4  A: 10,000 F_GETLKs: P0 growth, cost per call */
#include <errno.h>
#include <fcntl.h>
#include <jpidef.h>
#include <lib$routines.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static char role;
static void step(int n) { char f[32]; FILE *fp; sprintf(f, "k_%d.step", n); fp = fopen(f, "w"); if (fp) fclose(fp); }
static void await(int n)
{
  char f[32]; int i;
  sprintf(f, "k_%d.step", n);
  for (i = 0; i < 600 && access(f, F_OK) != 0; i++) usleep(100000);
  if (i == 600) { printf("%c: timed out waiting for step %d\n", role, n); _exit(1); }
}
static void check(const char *what, int ok, const char *d) { printf("%c %-4s %-48s %s\n", role, ok ? "OK" : "BAD", what, d); fflush(stdout); }
static unsigned p0(void) { int item = JPI$_FREP0VA; unsigned v = 0; lib$getjpi(&item, 0, 0, &v, 0, 0); return v; }
static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec * 1e6 + t.tv_usec; }

static int lk(int fd, int cmd, int type, long start, struct flock *out)
{
  struct flock l;
  int r;
  memset(&l, 0, sizeof l);
  l.l_type = type; l.l_whence = SEEK_SET; l.l_start = start; l.l_len = 1;
  r = fcntl(fd, cmd, &l);
  if (out) *out = l;
  return r;
}

int main(int argc, char **argv)
{
  struct flock g;
  char d[160];
  int fd, r, i;
  role = argc > 1 ? argv[1][0] : 'a';
  fd = open("getlk_test.lck", O_RDWR | O_CREAT, 0644, "shr=get,put,upd");
  if (role == 'b') {
    lk(fd, F_SETLK, F_WRLCK, 2, 0);
    step(1); await(2);
    lk(fd, F_SETLK, F_UNLCK, 2, 0);
    step(3); await(4);
  } else {
    await(1);
    r = lk(fd, F_GETLK, F_WRLCK, 2, &g);
    sprintf(d, "rc %d type %d (F_WRLCK %d) pid %d", r, g.l_type, F_WRLCK, (int) g.l_pid);
    check("K1 B's write lock on byte 2 is seen", r == 0 && g.l_type == F_WRLCK, d);
    step(2); await(3);
    r = lk(fd, F_GETLK, F_WRLCK, 2, &g);
    sprintf(d, "rc %d type %d (F_UNLCK %d)", r, g.l_type, F_UNLCK);
    check("K2 after B's release: none", r == 0 && g.l_type == F_UNLCK, d);
    lk(fd, F_SETLK, F_RDLCK, 4, 0);
    r = lk(fd, F_GETLK, F_WRLCK, 2, &g);
    sprintf(d, "rc %d type %d", r, g.l_type);
    check("K3 holding a read lock on byte 4: byte 2 free", r == 0 && g.l_type == F_UNLCK, d);
    lk(fd, F_SETLK, F_UNLCK, 4, 0);
    {
      unsigned before;
      double t0;
      for (i = 0; i < 500; i++) lk(fd, F_GETLK, F_WRLCK, 2, &g);   /* warm up */
      before = p0();
      t0 = now();
      for (i = 0; i < 10000; i++) lk(fd, F_GETLK, F_WRLCK, 2, &g);
      sprintf(d, "P0 +%u bytes, %.1f us per call", p0() - before, (now() - t0) / 10000);
      check("K4 10,000 F_GETLK", p0() - before < 4096, d);
    }
    step(4);
  }
  close(fd);
  return 0;
}
