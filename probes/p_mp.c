/* p_mp.c - two processes using one file the way SQLite's unix VFS does.
   Run "p_mp a" and "p_mp b" at the same time (p_mp.com); they take turns
   through marker files (mp_<n>.step).  Each line printed is one check:
   OK means POSIX behaviour, BAD means SQLite would go wrong.

   T1  locks near 1 GB on a small file (PENDING_BYTE 0x40000000 and the
       SHARED range after it), between processes
   T2  write visibility: B writes a page A has written before (both fsync)
   T3  file size: A's fstat after B extends the file
   T4  ftruncate by B, size seen by A
   T5  mmap MAP_SHARED of one file in both processes (WAL's -shm) */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define PENDING 0x40000000L
#define DB "mp_test.db"
static char role;

static void step(int n)          /* say step n is done */
{
  char f[32]; FILE *fp;
  sprintf(f, "mp_%d.step", n);
  fp = fopen(f, "w"); if (fp) fclose(fp);
}

static void await(int n)         /* wait for the other side's step n */
{
  char f[32]; int i;
  sprintf(f, "mp_%d.step", n);
  for (i = 0; i < 600 && access(f, F_OK) != 0; i++) usleep(100000);
  if (i == 600) { printf("%c: timed out waiting for step %d\n", role, n); exit(1); }
}

static int lk(int fd, int type, long start, long len)
{
  struct flock l;
  memset(&l, 0, sizeof l);
  l.l_type = type; l.l_whence = SEEK_SET; l.l_start = start; l.l_len = len;
  return fcntl(fd, F_SETLK, &l);
}

static void check(const char *what, int ok, const char *detail)
{
  printf("%c %-4s %-46s %s\n", role, ok ? "OK" : "BAD", what, detail);
}

int main(int argc, char **argv)
{
  char page[4096], c, d[80];
  struct stat st;
  int fd, r, e;

  role = argc > 1 ? argv[1][0] : '?';
  if (role == 'a') {
    memset(page, 'A', sizeof page);
    fd = open(DB, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("a: open"); return 1; }
    pwrite(fd, page, sizeof page, 0); pwrite(fd, page, sizeof page, 4096);
    fsync(fd);
    /* T1: A holds a read lock on the SHARED range, as a reader does */
    r = lk(fd, F_RDLCK, PENDING + 2, 510);
    sprintf(d, "rc %d errno %d", r, r ? errno : 0);
    check("T1 read lock SHARED range (0x40000002,510)", r == 0, d);
    step(1); await(2);
    lk(fd, F_UNLCK, PENDING + 2, 510);
    /* T2: A writes page 0 first, then B overwrites it, then A reads */
    c = 'a'; pwrite(fd, &c, 1, 100); fsync(fd);
    step(3); await(4);
    pread(fd, &c, 1, 100);
    sprintf(d, "byte 100 is '%c' (B wrote 'b')", c);
    check("T2 read after the other process's write+fsync", c == 'b', d);
    lk(fd, F_RDLCK, PENDING + 2, 510); pread(fd, &c, 1, 100); lk(fd, F_UNLCK, PENDING + 2, 510);
    sprintf(d, "byte 100 is '%c'", c);
    check("T2b same, read under a fresh read lock", c == 'b', d);
    /* T3: B appended page 2; A asks for the size */
    fstat(fd, &st);
    sprintf(d, "fstat %ld, lseek(END) %ld (expect 12288)", (long) st.st_size, (long) lseek(fd, 0, SEEK_END));
    check("T3 size after the other process extended it", st.st_size == 12288, d);
    r = pread(fd, &c, 1, 8192 + 5);
    sprintf(d, "pread rc %d byte '%c'", r, r == 1 ? c : '-');
    check("T3b read the other process's new page", r == 1 && c == 'B', d);
    step(5); await(6);
    fstat(fd, &st);
    sprintf(d, "fstat %ld, lseek(END) %ld (expect 4096)", (long) st.st_size, (long) lseek(fd, 0, SEEK_END));
    check("T4 size after the other process's ftruncate", st.st_size == 4096, d);
    close(fd);
    /* T5 */
    {
      int sfd = open("mp_test.db-shm", O_RDWR | O_CREAT | O_TRUNC, 0644);
      volatile int *p;
      ftruncate(sfd, 32768);
      p = mmap(0, 32768, PROT_READ | PROT_WRITE, MAP_SHARED, sfd, 0);
      sprintf(d, "%p errno %d", (void *) p, p == MAP_FAILED ? errno : 0);
      check("T5 mmap MAP_SHARED 32 KB", p != MAP_FAILED, d);
      if (p != MAP_FAILED) p[10] = 4242;
      step(7); await(8);
      if (p != MAP_FAILED) {
        sprintf(d, "word 20 = %d (B stored 1717)", p[20]);
        check("T5b see the other process's store", p[20] == 1717, d);
      }
    }
    step(9);
  } else if (role == 'b') {
    await(1);
    fd = open(DB, O_RDWR, 0644);
    e = errno;
    sprintf(d, "fd %d errno %d", fd, fd < 0 ? e : 0);
    check("T0 second process opens the file O_RDWR", fd >= 0, d);
    if (fd < 0) { step(2); step(4); step(6); step(8); return 1; }
    r = lk(fd, F_WRLCK, PENDING, 1);
    sprintf(d, "rc %d", r);
    check("T1 write lock PENDING byte (no conflict)", r == 0, d);
    r = lk(fd, F_WRLCK, PENDING + 2, 510); e = errno;
    sprintf(d, "rc %d errno %d (want -1 EAGAIN/EACCES)", r, r ? e : 0);
    check("T1 write lock SHARED range refused", r == -1 && (e == EAGAIN || e == EACCES), d);
    lk(fd, F_UNLCK, PENDING, 1);
    /* B writes page 0 once before (as a writer process does) */
    c = 'x'; pwrite(fd, &c, 1, 200); fsync(fd);
    step(2); await(3);
    pread(fd, &c, 1, 100);
    sprintf(d, "byte 100 is '%c' (A wrote 'a')", c);
    check("T2 read after the other process's write+fsync", c == 'a', d);
    c = 'b'; pwrite(fd, &c, 1, 100);
    memset(page, 'B', sizeof page); pwrite(fd, page, sizeof page, 8192);
    fsync(fd);
    step(4); await(5);
    r = ftruncate(fd, 4096); fsync(fd);
    sprintf(d, "rc %d", r);
    check("T4 ftruncate to 4096", r == 0, d);
    step(6);
    close(fd);
    await(7);
    {
      int sfd = open("mp_test.db-shm", O_RDWR, 0644);
      volatile int *p = mmap(0, 32768, PROT_READ | PROT_WRITE, MAP_SHARED, sfd, 0);
      if (p == MAP_FAILED) { check("T5 mmap in second process", 0, strerror(errno)); }
      else {
        sprintf(d, "word 10 = %d (A stored 4242)", p[10]);
        check("T5b see the other process's store", p[10] == 4242, d);
        p[20] = 1717;
      }
      step(8);
    }
    await(9);
    usleep(1000000);   /* let A exit and close its log */
  }
  return 0;
}
