/* p_misc.c - single-process file behaviour SQLite's unix VFS relies on.
   One line per check: OK (POSIX) or BAD, with what was seen. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void check(const char *what, int ok, const char *detail)
{
  printf("MISC %-4s %-44s %s\n", ok ? "OK" : "BAD", what, detail);
}

static int versions(const char *name)   /* how many versions does a name have? */
{
  char cmd[200], line[200]; FILE *p; int n = 0;
  sprintf(cmd, "pipe directory/nohead/notrail %s;* | search sys$pipe \";\" ", name);
  p = popen(cmd, "r");
  if (!p) return -1;
  while (fgets(line, sizeof line, p)) n++;
  pclose(p);
  return n;
}

int main(void)
{
  char buf[4096], d[200], *s;
  struct stat st;
  int fd, r, e;

  memset(buf, 'Q', sizeof buf);
  unlink("misc.db"); unlink("misc.db");
  fd = open("misc.db", O_RDWR | O_CREAT, 0644);
  pwrite(fd, buf, 4096, 0); pwrite(fd, buf, 4096, 4096);
  fstat(fd, &st);
  sprintf(d, "fstat %ld after 8192 written, before fsync", (long) st.st_size);
  check("fstat size of an open file", st.st_size == 8192, d);
  fsync(fd); fstat(fd, &st);
  sprintf(d, "fstat %ld after fsync", (long) st.st_size);
  check("fstat size after fsync", st.st_size == 8192, d);
  r = ftruncate(fd, 4096); fstat(fd, &st);
  sprintf(d, "rc %d, fstat %ld, lseek(END) %ld", r, (long) st.st_size, (long) lseek(fd, 0, SEEK_END));
  check("ftruncate 8192 -> 4096", st.st_size == 4096, d);
  r = pread(fd, buf, 10, 100000);
  sprintf(d, "rc %d", r);
  check("pread past end returns 0", r == 0, d);
  close(fd); stat("misc.db", &st);
  sprintf(d, "size %ld after close", (long) st.st_size);
  check("size after close (pread past end moved EOF?)", st.st_size == 4096, d);

  /* journal: created, written, deleted - repeatedly */
  for (r = 0; r < 3; r++) {
    fd = open("misc.db-journal", O_RDWR | O_CREAT, 0644);
    write(fd, buf, 512); fsync(fd); close(fd);
    unlink("misc.db-journal");
  }
  sprintf(d, "access after unlink: %d; versions %d", access("misc.db-journal", F_OK),
          versions("misc.db-journal"));
  check("-journal create/unlink x3 leaves nothing", access("misc.db-journal", F_OK) != 0, d);
  fd = open("misc.db-journal", O_RDWR | O_CREAT | O_TRUNC, 0644); close(fd);
  fd = open("misc.db-journal", O_RDWR | O_CREAT | O_TRUNC, 0644); close(fd);
  sprintf(d, "%d versions", versions("misc.db-journal"));
  check("O_TRUNC of an existing file keeps 1 version", versions("misc.db-journal") == 1, d);
  unlink("misc.db-journal"); unlink("misc.db-journal");
  fd = open("misc.db-journal", O_RDWR | O_CREAT, 0644);
  r = unlink("misc.db-journal"); e = errno;
  sprintf(d, "rc %d errno %d", r, r ? e : 0);
  check("unlink of an open file", r == 0, d);
  close(fd);
  fd = open("misc.db-wal", O_RDWR | O_CREAT, 0644);
  sprintf(d, "fd %d", fd);
  check("open misc.db-wal (dash in the type)", fd >= 0, d); close(fd); unlink("misc.db-wal");
  fd = open("Misc.Two.db", O_RDWR | O_CREAT, 0644);
  sprintf(d, "fd %d", fd);
  check("open Misc.Two.db (two dots, case)", fd >= 0, d); close(fd); unlink("Misc.Two.db");

  /* directories and names */
  fd = open(".", O_RDONLY);
  sprintf(d, "fd %d errno %d", fd, fd < 0 ? errno : 0);
  check("open(\".\") for a directory fsync", fd >= 0, d); if (fd >= 0) close(fd);
  s = getcwd(buf, sizeof buf);
  sprintf(d, "%s", s ? s : "NULL");
  check("getcwd is a Unix path", s && buf[0] == '/', d);
  s = realpath("misc.db", buf); e = errno;
  sprintf(d, "%s errno %d", s ? s : "NULL", s ? 0 : e);
  check("realpath", s != 0, d);
  r = readlink("misc.db", buf, sizeof buf); e = errno;
  sprintf(d, "rc %d errno %d (EINVAL = not a link)", r, e);
  check("readlink on a plain file", r == -1 && e == EINVAL, d);
  r = lstat("misc.db", &st);
  sprintf(d, "rc %d", r);
  check("lstat", r == 0, d);
  s = getenv("TMPDIR");
  sprintf(d, "TMPDIR=%s; access(/tmp)=%d; access(/sys$scratch)=%d", s ? s : "(none)",
          access("/tmp", W_OK), access("/sys$scratch", W_OK));
  check("a temp directory", 1, d);
  r = mkdir("misc_dir", 0755);
  sprintf(d, "mkdir %d rmdir %d", r, rmdir("misc_dir"));
  check("mkdir/rmdir", r == 0, d);
  {
    time_t t = time(0); struct tm tm;
    sprintf(d, "sizeof(time_t) %d, localtime_r %s", (int) sizeof t,
            localtime_r(&t, &tm) ? "ok" : "NULL");
    check("time", 1, d);
  }
  sprintf(d, "sizeof(off_t) %d, long %d, void* %d", (int) sizeof(off_t), (int) sizeof(long),
          (int) sizeof(void *));
  check("type sizes", 1, d);
  sprintf(d, "geteuid %d getpid %d pagesize %d", (int) geteuid(), (int) getpid(), (int) getpagesize());
  check("ids", 1, d);
  unlink("misc.db");
  return 0;
}
