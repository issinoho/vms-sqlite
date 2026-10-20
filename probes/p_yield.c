/* p_yield.c - does sched_yield() let another thread of the process run?
   threadtest4 retries a busy VACUUM 10 times with sched_yield() between,
   while other threads finish their reads; it fails 7 of 9 runs after the
   VFS stopped making blocking calls at every transaction (D8).  The test
   images are linked without /THREADS_ENABLE: one kernel thread.
     Y1  two threads, each 2000 x (append its id to a shared log; yield):
         how many times did control change hands?  (~4000 if yield works)
     Y2  the same with no yield at all (switches only at quantum end) */
#include <pthread.h>
#if !defined(__VMS) || defined(__clang__)
#include <sched.h>
#endif
#include <stdio.h>

#define N 2000
static char log_[2 * N + 10];
static int nLog, doYield;

static void *run(void *arg)
{
  int i;
  for (i = 0; i < N; i++) {
    log_[nLog++] = *(char *) arg;
    if (doYield) sched_yield();
    else { volatile int k; for (k = 0; k < 20000; k++) ; }
  }
  return 0;
}

static void test(const char *id, int y)
{
  pthread_t a, b;
  static char ca = 'a', cb = 'b';
  int i, sw = 0;
  nLog = 0; doYield = y;
  pthread_create(&a, 0, run, &ca);
  pthread_create(&b, 0, run, &cb);
  pthread_join(a, 0);
  pthread_join(b, 0);
  for (i = 1; i < nLog; i++) if (log_[i] != log_[i - 1]) sw++;
  printf("%-4s %s: %d entries, %d changes of thread\n", id, y ? "sched_yield" : "no yield", nLog, sw);
}

int main(void)
{
  test("Y1", 1);
  test("Y2", 0);
  return 0;
}
