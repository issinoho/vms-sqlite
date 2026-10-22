# Phase 3: PHP (vms-php 8.5.12-vms2)

**Status:** started (2026-10-22). The work is in `~/projects/vms-php` (its rules and records
apply there: its D28 for the extensions, its PORTING_LOG); this file keeps the SQLite side.

## Plan (from SQLITE_OPENVMS_PLAN.md, the user's choices of 2026-10-22)

1. The library reaches PHP by `tools/deploy.sh` (D10): done, the clang tree of the final
   Phase 2 build.
2. vms-php: `sqlite3`, `pdo`, `pdo_sqlite` and `pdo_mysql` enabled, linked with
   `LIBSQLITE3.OLB`; the release becomes 8.5.12-vms2.
3. Their `.phpt` sets against a Linux reference built with the same options (pdo_mysql's
   need a MySQL server: as vms-php's D14 for mysqli).
4. A new phpBB 3.3 test board on SQLite: installed fresh, its own directory and port
   (e.g. :18081) and FastCGI pool, under vms-lighttpd's test server; the :18080 board and
   the live board untouched. Then `test_phpbb.py` and a load run: the acceptance test.
