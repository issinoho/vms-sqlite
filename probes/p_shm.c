/* p_shm.c - can WAL's -shm work with the C RTL's mmap()?  (p_mp T5 mapped one
   32 KB region made before either process looked.)  WAL grows the -shm in
   32 KB regions that every process maps when it needs them; the C RTL keeps
   its own end of file per descriptor and writes it back at close (D5).
   Run "p_shm a" and "p_shm b" together (vms_probe.com).

   W1  A creates the file (32 KB) and maps region 0; B opens and maps it;
       stores seen both ways
   W2  A grows the file to 64 KB (ftruncate) and maps region 1; B, whose
       descriptor was opened at 32 KB, maps region 1; stores both ways
   W3  B grows it to 96 KB by writing a byte at the end (not ftruncate), A
       maps region 2 (can A, which did not grow it, map it?)
   W4  B unmaps and closes: A still sees 96 KB (stat) and its mappings work
   W5  B opens and maps all three again: A's earlier stores are there
   W6  B: 500 map/unmap cycles of a 32 KB region: P0 growth, map address
   W7  a shared counter, 100,000 increments each by A and B (atomic
       builtins) on the mapping: total 200,000? */
#include <errno.h>
#include <fcntl.h>
#include <jpidef.h>
#include <lib$routines.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef __clang__
#include <builtins.h>
#endif

#ifdef __clang__
#  define ATOMIC_INC(p) __sync_fetch_and_add((p), 1)
#else
#  define ATOMIC_INC(p) __ATOMIC_ADD_LONG((void *) (p), 1)
#endif

#define F "shm_test.db-shm"
#define R 32768
static char role;

static void step(int n) { char f[32]; FILE *fp; sprintf(f, "s_%d.step", n); fp = fopen(f, "w"); if (fp) fclose(fp); }
static void await(int n)
{
  char f[32]; int i;
  sprintf(f, "s_%d.step", n);
  for (i = 0; i < 600 && access(f, F_OK) != 0; i++) usleep(100000);
  if (i == 600) { printf("%c: timed out waiting for step %d\n", role, n); exit(1); }
}
static void check(const char *what, int ok, const char *d) { printf("%c %-4s %-46s %s\n", role, ok ? "OK" : "BAD", what, d); fflush(stdout); }
static unsigned p0(void) { int item = JPI$_FREP0VA; unsigned v = 0; lib$getjpi(&item, 0, 0, &v, 0, 0); return v; }

static volatile int *map(int fd, int region, char *d)
{
  void *p = mmap(0, R, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t) region * R);
  sprintf(d, "region %d at %p errno %d", region, p, p == MAP_FAILED ? errno : 0);
  return p == MAP_FAILED ? 0 : (volatile int *) p;
}
static long fsize(void) { struct stat st; return stat(F, &st) ? -1 : (long) st.st_size; }

int main(int argc, char **argv)
{
  volatile int *m[3] = { 0, 0, 0 };
  char d[200];
  int fd, i;
  role = argc > 1 ? argv[1][0] : 'a';

  if (role == 'a') {
    fd = open(F, O_RDWR | O_CREAT | O_TRUNC, 0644, "shr=get,put,upd");
    ftruncate(fd, R);
    m[0] = map(fd, 0, d); check("W1 map region 0", m[0] != 0, d);
    if (m[0]) m[0][10] = 1010;
    step(1); await(2);
    sprintf(d, "word 20 = %d (B stored 2020)", m[0] ? m[0][20] : -1);
    check("W1 see B's store in region 0", m[0] && m[0][20] == 2020, d);
    ftruncate(fd, 2 * R);
    m[1] = map(fd, 1, d); check("W2 grow to 64 KB (ftruncate), map region 1", m[1] != 0, d);
    if (m[1]) m[1][30] = 3030;
    step(3); await(4);
    sprintf(d, "word 40 = %d (B stored 4040)", m[1] ? m[1][40] : -1);
    check("W2 see B's store in region 1", m[1] && m[1][40] == 4040, d);
    await(5);                                   /* B grew it to 96 KB */
    sprintf(d, "stat size %ld", fsize());
    check("W3 the size B made is seen", fsize() == 3 * R, d);
    m[2] = map(fd, 2, d); check("W3 map region 2, grown by B", m[2] != 0, d);
    if (m[2]) m[2][50] = 5050;
    step(6); await(7);                          /* B unmapped and closed */
    sprintf(d, "stat size %ld", fsize());
    check("W4 after B's close the size is still 96 KB", fsize() == 3 * R, d);
    sprintf(d, "words %d %d %d", m[0] ? m[0][10] : -1, m[1] ? m[1][30] : -1, m[2] ? m[2][50] : -1);
    check("W4 own mappings intact", m[0] && m[1] && m[2] && m[0][10] == 1010 && m[1][30] == 3030 && m[2][50] == 5050, d);
    step(8); await(9);
    for (i = 0; i < 100000; i++) ATOMIC_INC(&m[0][100]);
    step(10); await(11);
    sprintf(d, "counter %d", m[0][100]);
    check("W7 200,000 atomic increments from two processes", m[0][100] == 200000, d);
    step(12);
    close(fd);
  } else {
    await(1);
    fd = open(F, O_RDWR, 0644, "shr=get,put,upd");
    m[0] = map(fd, 0, d); check("W1 map region 0 (A made it)", m[0] != 0, d);
    sprintf(d, "word 10 = %d (A stored 1010)", m[0] ? m[0][10] : -1);
    check("W1 see A's store in region 0", m[0] && m[0][10] == 1010, d);
    if (m[0]) m[0][20] = 2020;
    step(2); await(3);
    m[1] = map(fd, 1, d); check("W2 map region 1, grown by A after our open", m[1] != 0, d);
    sprintf(d, "word 30 = %d (A stored 3030)", m[1] ? m[1][30] : -1);
    check("W2 see A's store in region 1", m[1] && m[1][30] == 3030, d);
    if (m[1]) m[1][40] = 4040;
    step(4);
    { char c = 0; lseek(fd, 3 * R - 1, SEEK_SET); write(fd, &c, 1); fsync(fd); }
    sprintf(d, "stat size %ld", fsize());
    check("W3 grow to 96 KB by writing the last byte", fsize() == 3 * R, d);
    step(5); await(6);
    munmap((void *) m[0], R); munmap((void *) m[1], R);
    close(fd);
    step(7); await(8);
    fd = open(F, O_RDWR, 0644, "shr=get,put,upd");
    m[0] = map(fd, 0, d); m[1] = map(fd, 1, d); m[2] = map(fd, 2, d);
    sprintf(d, "words %d %d %d", m[0] ? m[0][10] : -1, m[1] ? m[1][30] : -1, m[2] ? m[2][50] : -1);
    check("W5 open again, map 3 regions, A's stores there",
          m[0] && m[1] && m[2] && m[0][10] == 1010 && m[1][30] == 3030 && m[2][50] == 5050, d);
    {
      unsigned before = p0();
      void *first = 0;
      for (i = 0; i < 500; i++) {
        void *p = mmap(0, R, PROT_READ | PROT_WRITE, MAP_SHARED, fd, R);
        if (p == MAP_FAILED) break;
        if (!first) first = p;
        munmap(p, R);
      }
      sprintf(d, "%d cycles, P0 +%u bytes, at %p", i, p0() - before, first);
      check("W6 map/unmap cycles", i == 500 && p0() - before < 4096, d);
    }
    step(9);
    for (i = 0; i < 100000; i++) ATOMIC_INC(&m[0][100]);
    step(11); await(10); await(12);
    close(fd);
  }
  return 0;
}
