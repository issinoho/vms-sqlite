/* p_exitlk.c - at image exit, are a process's fcntl() locks released before
   or after its atexit() handlers have run?  os_vms.c writes a dying
   process's buffered pages and end of file in an atexit handler (PORTING_LOG
   #14), and relies on still holding its locks meanwhile: mptest config02
   corrupts a database now and then just after another process rolls back
   the dead one's hot journal.
   Run "p_exitlk a" and "p_exitlk b" together (vms_probe.com).
     E1  A takes a write lock, registers a handler (marker files at its start
         and end, 3 s apart), calls exit(1).  B polls for the lock every 20 ms:
         when it gets it, has A's handler finished?
     E2  the same with the handler registered before the lock was taken and
         the lock file opened (as os_vms.c: sqlite3_os_init, then opens) */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char role;
static void mark(const char *f) { FILE *fp = fopen(f, "w"); if (fp) fclose(fp); }
static int there(const char *f) { return access(f, F_OK) == 0; }
static void await(const char *f)
{
  int i;
  for (i = 0; i < 600 && !there(f); i++) usleep(100000);
  if (i == 600) { printf("%c: timed out waiting for %s\n", role, f); exit(1); }
}
static int lk(int fd, int type)
{
  struct flock l;
  memset(&l, 0, sizeof l);
  l.l_type = type; l.l_whence = SEEK_SET; l.l_start = 0; l.l_len = 1;
  return fcntl(fd, F_SETLK, &l);
}

static void handler(void)
{
  mark("ex_hstart.step");
  sleep(3);
  mark("ex_hend.step");
}

int main(int argc, char **argv)
{
  int fd, i, early;
  role = argc > 1 ? argv[1][0] : 'a';
  early = argc > 2 && argv[2][0] == '2';
  if (role == 'a') {
    if (early) atexit(handler);
    fd = open("exitlk_test.lck", O_RDWR | O_CREAT, 0644, "shr=get,put,upd");
    if (lk(fd, F_WRLCK)) { printf("a: lock failed errno %d\n", errno); return 1; }
    if (!early) atexit(handler);
    mark("ex_locked.step");
    exit(1);
  } else {
    fd = open("exitlk_test.lck", O_RDWR | O_CREAT, 0644, "shr=get,put,upd");
    await("ex_locked.step");
    for (i = 0; i < 1000 && lk(fd, F_WRLCK); i++) usleep(20000);
    printf("b %-4s E%c lock free after %d polls: A's handler %s\n",
           i < 1000 && there("ex_hend.step") ? "OK" : "BAD", early ? '2' : '1', i,
           there("ex_hend.step") ? "had finished" :
           there("ex_hstart.step") ? "was still running (its writes not yet done)" : "had not started");
    lk(fd, F_UNLCK);
    close(fd);
  }
  return 0;
}
