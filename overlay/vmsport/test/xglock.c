/* xglock.c - the lock steps of CROSSGROUP.COM (D5's cross-group test): two
   accounts in different UIC groups take turns holding a database while the
   other tries it, coordinated by marker files in the shared directory.

     xglock hold <db> <seconds> <tag>  BEGIN EXCLUSIVE (and a write), make
                                       <tag>_HOLDING.TXT, hold, COMMIT, make
                                       <tag>_RELEASED.TXT
     xglock try <db> <expect>          a read and BEGIN IMMEDIATE with no busy
                                       wait: prints OK or BUSY, and PASS or
                                       FAIL against <expect> (OK or BUSY)
     xglock wait <file> <seconds>      wait for a marker file (DCL's WAIT is
                                       not usable over ssh)
   Exit status: success, or %X10000002 (error) on FAIL or timeout. */
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FAIL_STATUS 0x10000002     /* error, and DCL shows no message */

static void mark(const char *tag, const char *what)
{
  char f[64];
  FILE *fp;
  snprintf(f, sizeof f, "%s_%s.TXT", tag, what);
  fp = fopen(f, "w");
  if (fp) fclose(fp);
}

static int wait_for(const char *f, int secs)
{
  int i;
  for (i = 0; i < secs * 10; i++) {
    if (access(f, 0) == 0) return 1;
    usleep(100000);
  }
  return 0;
}

int main(int argc, char **argv)
{
  sqlite3 *db;
  int rc;
  if (argc >= 4 && !strcmp(argv[1], "wait")) {
    if (wait_for(argv[2], atoi(argv[3]))) { printf("XGLOCK: %s is there\n", argv[2]); return 1; }
    printf("XGLOCK: FAIL, no %s after %s s\n", argv[2], argv[3]);
    return FAIL_STATUS;
  }
  if (argc >= 5 && !strcmp(argv[1], "hold")) {
    if (sqlite3_open(argv[2], &db)) { printf("XGLOCK: FAIL, open %s\n", argv[2]); return FAIL_STATUS; }
    sqlite3_busy_timeout(db, 60000);
    rc = sqlite3_exec(db, "BEGIN EXCLUSIVE; INSERT INTO xg VALUES(1);", 0, 0, 0);
    if (rc) { printf("XGLOCK: FAIL, BEGIN EXCLUSIVE: %s\n", sqlite3_errmsg(db)); return FAIL_STATUS; }
    printf("XGLOCK: holding EXCLUSIVE for %s s\n", argv[3]);
    fflush(stdout);
    mark(argv[4], "HOLDING");
    sleep(atoi(argv[3]));
    rc = sqlite3_exec(db, "COMMIT", 0, 0, 0);
    sqlite3_close(db);
    mark(argv[4], "RELEASED");
    printf("XGLOCK: released (%s)\n", rc ? "COMMIT failed" : "committed");
    return rc ? FAIL_STATUS : 1;
  }
  if (argc >= 4 && !strcmp(argv[1], "try")) {
    const char *got;
    int rr, rw;
    if (sqlite3_open(argv[2], &db)) { printf("XGLOCK: FAIL, open %s\n", argv[2]); return FAIL_STATUS; }
    sqlite3_busy_timeout(db, 0);
    rr = sqlite3_exec(db, "SELECT count(*) FROM xg;", 0, 0, 0);
    rw = sqlite3_exec(db, "BEGIN IMMEDIATE;", 0, 0, 0);
    if (rw == SQLITE_OK) sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
    sqlite3_close(db);
    got = rr == SQLITE_OK && rw == SQLITE_OK ? "OK" : rr == SQLITE_BUSY && rw == SQLITE_BUSY ? "BUSY" : "MIXED";
    printf("XGLOCK: read %d, write %d: %s, expected %s: %s\n", rr, rw, got, argv[3],
           !strcmp(got, argv[3]) ? "PASS" : "FAIL");
    return !strcmp(got, argv[3]) ? 1 : FAIL_STATUS;
  }
  printf("usage: xglock hold <db> <s> <tag> | try <db> OK|BUSY | wait <file> <s>\n");
  return FAIL_STATUS;
}
