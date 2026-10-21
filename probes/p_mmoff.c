/* p_mmoff.c - which mmap() offsets and lengths does the C RTL take?
   (p_shm: every MAP_SHARED mapping of a file at a non-zero offset fails,
   errno EVMSERR.)  A 256 KB file; each line: offset, length, flags, the
   result (address, or errno / vaxc$errno and its message), and for a
   mapping, the word at its start (the file holds its own offsets). */
#include <descrip.h>
#include <errno.h>
#include <fcntl.h>
#include <starlet.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int fd;

static void msg(unsigned st, char *out)
{
  char buf[256];
  unsigned short len = 0;
  struct dsc$descriptor d;
  d.dsc$w_length = sizeof buf - 1; d.dsc$a_pointer = buf;
  d.dsc$b_dtype = DSC$K_DTYPE_T; d.dsc$b_class = DSC$K_CLASS_S;
  if (sys$getmsg(st, &len, &d, 15, 0) & 1) { buf[len] = 0; strcpy(out, buf); } else strcpy(out, "?");
}

static void try(long off, long len, int flags, const char *fl)
{
  char m[256] = "";
  void *p = mmap(0, len, PROT_READ | PROT_WRITE, flags, fd, (off_t) off);
  if (p == MAP_FAILED) {
    int e = errno; unsigned v = vaxc$errno;
    if (e == EVMSERR) msg(v, m);
    printf("off %7ld len %7ld %-14s FAIL errno %d vaxc$errno %%X%08X %s\n", off, len, fl, e, v, m);
  } else {
    printf("off %7ld len %7ld %-14s OK   at %p word %d\n", off, len, fl, p, *(int *) p);
    munmap(p, len);
  }
}

int main(void)
{
  static int blk[2048];
  int i, k;
  fd = open("mmoff_test.db-shm", O_RDWR | O_CREAT | O_TRUNC, 0644, "shr=get,put,upd");
  for (k = 0; k < 32; k++) {                    /* 32 x 8 KB, each starting with its offset */
    for (i = 0; i < 2048; i++) blk[i] = k * 8192 + i * 4;
    write(fd, blk, sizeof blk);
  }
  fsync(fd);
  printf("page size %d\n", getpagesize());
  try(0, 32768, MAP_SHARED, "SHARED");
  try(0, 262144, MAP_SHARED, "SHARED");
  try(512, 32768, MAP_SHARED, "SHARED");
  try(8192, 32768, MAP_SHARED, "SHARED");
  try(32768, 32768, MAP_SHARED, "SHARED");
  try(65536, 32768, MAP_SHARED, "SHARED");
  try(131072, 32768, MAP_SHARED, "SHARED");
  try(32768, 32768, MAP_SHARED | MAP_VARIABLE, "SHARED|VARIABLE");
  try(32768, 32768, MAP_PRIVATE, "PRIVATE");
  try(32768, 32768, MAP_SHARED | MAP_FILE, "SHARED|FILE");
  {
    /* two mappings of the file at once */
    char m[256] = "";
    void *a = mmap(0, 32768, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0), *b;
    int fd2;
    printf("hold region 0 at %p\n", a);
    printf("  same fd:    "); try(32768, 32768, MAP_SHARED, "SHARED");
    fd2 = open("mmoff_test.db-shm", O_RDWR, 0644, "shr=get,put,upd");
    { int sv = fd; fd = fd2; printf("  second fd:  "); try(32768, 32768, MAP_SHARED, "SHARED"); fd = sv; }
    b = mmap((char *) a + 32768, 32768, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 32768);
    if (b == MAP_FAILED) { if (errno == EVMSERR) msg(vaxc$errno, m); printf("  MAP_FIXED after it: FAIL errno %d %%X%08X %s\n", errno, vaxc$errno, m); }
    else { printf("  MAP_FIXED after it: OK at %p word %d\n", b, *(int *) b); munmap(b, 32768); }
    /* region 0 and 1 in two descriptors, both mapped: same memory as each other's file? */
    {
      int *r1 = mmap(0, 32768, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 32768);
      if (r1 != MAP_FAILED) {
        r1[5] = 777;
        printf("  region 1 via fd2 at %p, store 777; read back via a fresh mapping: ", (void *) r1);
        { int fd3 = open("mmoff_test.db-shm", O_RDWR, 0644, "shr=get,put,upd");
          int *r1b = mmap(0, 32768, PROT_READ | PROT_WRITE, MAP_SHARED, fd3, 32768);
          if (r1b == MAP_FAILED) printf("FAIL errno %d %%X%08X\n", errno, vaxc$errno);
          else { printf("%d\n", r1b[5]); munmap(r1b, 32768); }
          close(fd3); }
        munmap(r1, 32768);
      }
    }
    munmap(a, 32768);
    close(fd2);
  }
  close(fd);
  return 0;
}
