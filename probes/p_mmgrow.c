/* p_mmgrow.c - after growing a file, can the C RTL map the new part?  And
   what does a MAP_SHARED mapping make (p_shm: regions added by ftruncate()
   could not be mapped, even from a fresh descriptor).
     G1  create, ftruncate to 32 KB, map offset 0
     G2  ftruncate to 64 KB, map offset 32 KB (p_shm: fails)
     G3  write() 32 KB of zeros at 64 KB, fsync, map offset 64 KB
     G4  a fresh descriptor: map offsets 32 KB and 64 KB
     G5  with region 0 mapped: the global sections in the system that the
         mapping may be (INSTALL LIST/GLOBAL, looking for the file's name
         or the C RTL's)
     G6  with offset 0 mapped (section = the file at that time), grow the
         file by 32 KB and map the new part: does the section grow?
     G7  the same once nothing is mapped */
#include <descrip.h>
#include <errno.h>
#include <fcntl.h>
#include <lib$routines.h>
#include <starlet.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define F "mmgrow_test.db-shm"

static void try(const char *id, int fd, long off)
{
  void *p = mmap(0, 32768, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t) off);
  if (p == MAP_FAILED) printf("%-3s map offset %6ld: FAIL errno %d vaxc$errno %%X%08X\n", id, off, errno, vaxc$errno);
  else { printf("%-3s map offset %6ld: OK at %p\n", id, off, p); munmap(p, 32768); }
}

int main(void)
{
  static char z[32768];
  int fd, fd2;
  void *keep;
  fd = open(F, O_RDWR | O_CREAT | O_TRUNC, 0644, "shr=get,put,upd");
  ftruncate(fd, 32768);
  try("G1", fd, 0);
  ftruncate(fd, 65536);
  try("G2", fd, 32768);
  lseek(fd, 65536, SEEK_SET);
  write(fd, z, sizeof z);
  fsync(fd);
  try("G3", fd, 65536);
  fd2 = open(F, O_RDWR, 0644, "shr=get,put,upd");
  try("G4", fd2, 32768);
  try("G4", fd2, 65536);
  close(fd2);
  keep = mmap(0, 32768, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  printf("G5  holding offset 0 at %p; global sections:\n", keep);
  fflush(stdout);
  {
    $DESCRIPTOR(cmd, "pipe install list/global | search sys$pipe \"MMGROW\",\"C$\",\"DECC\" ");
    unsigned st = lib$spawn(&cmd, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    printf("G5  (spawn status %%X%08X)\n", st);
  }
  {
    long size = lseek(fd, 0, SEEK_END);
    lseek(fd, size, SEEK_SET);
    write(fd, z, sizeof z);
    fsync(fd);
    printf("G6  holding offset 0; grew %ld -> %ld\n", size, size + 32768);
    try("G6", fd, size - 32768);
    try("G6", fd, size);
  }
  munmap(keep, 32768);
  try("G7", fd, lseek(fd, 0, SEEK_END) - 32768);
  close(fd);
  return 0;
}
