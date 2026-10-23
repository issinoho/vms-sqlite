# Phase 3: PHP (vms-php 8.5.12-vms2)

**Status:** done (2026-10-23): the acceptance test passed, and vms-php 8.5.12-vms2 (with this
library) is installed on the live board and released
(https://github.com/issinoho/vms-php/releases/tag/v8.5.12-vms2). This repository is public
since the same day (https://github.com/issinoho/vms-sqlite), its history scrubbed of node
details first (an account name and UIC group numbers in D5/PHASE2, replaced by placeholders). The work is in `~/projects/vms-php` (its rules and records
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

## Results (x86-64, 2026-10-23; details in vms-php's `docs/PHASE6.md`)

| | |
|---|---|
| vms-php 8.5.12-vms2 built with `LIBSQLITE3.OLB` (clang) | 653 objects, 0 failed |
| `.phpt`: sqlite3, PDO (generic tests through pdo_sqlite), pdo_sqlite, 318 tests | 299 pass, 3 fail, 16 skip, as the Linux reference: 0 fail on VMS only |
| phpBB 3.3.19 on SQLite (vms-php D29), `test_phpbb.py` | 14 of 14 |
| Load: 30 min, 4 workers, 4 FastCGI processes writing one database | 2,319 requests, all 200, mean 3.05 s (the MySQL board: 1,417, 5.0 s); `integrity_check` ok; no crash |

**The plan's acceptance is met on x86-64:** the shell and libraries with both compilers,
the corpus, mptest and the durability test (Phase 2), several processes sharing one
database safely, and PHP's `sqlite3`/`pdo_sqlite` passing their tests and running a phpBB
board on SQLite under load with no crash.

Still to do (plan Phases 4-5): this project's own kit (PCSI VMSSQLITE, D3) and release; IA64
(D2, on hold).
