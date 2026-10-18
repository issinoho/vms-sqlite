/* waitdone.c - wait (up to P2 seconds) until P1 files named done_<n>.txt
   exist, n = 1..P1: lets a DCL procedure over ssh wait for its subprocesses
   without the DCL WAIT command. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv)
{
  int n = argc > 1 ? atoi(argv[1]) : 1, limit = argc > 2 ? atoi(argv[2]) : 600, t, i, have;
  char f[32];
  for (t = 0; t < limit * 10; t++) {
    for (have = 0, i = 1; i <= n; i++) { sprintf(f, "done_%d.txt", i); have += access(f, 0) == 0; }
    if (have == n) { printf("WAITDONE: %d done after %.1f s\n", n, t / 10.0); return 0; }
    usleep(100000);
  }
  printf("WAITDONE: timed out with %d of %d\n", have, n);
  return 1;
}
