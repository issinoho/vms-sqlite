/* kit_test.c - built against the installed kit by tools/vms_installcheck.com:
** with clang and the object library, and with VSI C (64-bit pointers) and
** SQLITE3$SHR. Opens P1, writes and reads it back, checks the version. */
#include <stdio.h>
#include <string.h>
#include "sqlite3.h"

static int fail(sqlite3 *db, const char *what) {
    printf("KIT_TEST: FAIL %s: %s\n", what, db ? sqlite3_errmsg(db) : "");
    return 1;
}

int main(int argc, char **argv) {
    sqlite3 *db = 0;
    sqlite3_stmt *st;
    long long sum = 0;
    int n = 0;
    if (argc < 2) return fail(0, "usage: kit_test <database>");
    if (strcmp(sqlite3_libversion(), SQLITE_VERSION) != 0 ||
        strcmp(sqlite3_version, SQLITE_VERSION) != 0)
        return fail(0, "version");
    if (sqlite3_open(argv[1], &db) != SQLITE_OK) return fail(db, "open");
    if (sqlite3_exec(db, "drop table if exists t; create table t(a integer, b text);"
                     "begin; with recursive c(x) as (select 1 union all select x+1 from c"
                     " where x<1000) insert into t select x, printf('row %d', x) from c;"
                     "commit;", 0, 0, 0) != SQLITE_OK)
        return fail(db, "write");
    if (sqlite3_prepare_v2(db, "select count(*), sum(a) from t where b like 'row %'",
                           -1, &st, 0) != SQLITE_OK)
        return fail(db, "prepare");
    if (sqlite3_step(st) == SQLITE_ROW) {
        n = sqlite3_column_int(st, 0);
        sum = sqlite3_column_int64(st, 1);
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    if (n != 1000 || sum != 500500) return fail(0, "read back");
    printf("KIT_TEST: PASS %s, %d rows\n", sqlite3_libversion(), n);
    return 0;
}
