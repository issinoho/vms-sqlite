/* noext.c - mptest calls sqlite3_enable_load_extension(), which this build
   omits (SQLITE_OMIT_LOAD_EXTENSION, D4): a stub that refuses, for mptest. */
#include "sqlite3.h"
int sqlite3_enable_load_extension(sqlite3 *db, int onoff)
{
  (void) db; (void) onoff;
  return SQLITE_ERROR;
}
