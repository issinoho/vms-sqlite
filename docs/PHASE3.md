# Phase 3: PHP (vms-php 8.5.12-vms2)

**Status:** done (2026-10-23): the acceptance test passed, and vms-php 8.5.12-vms2 (with this
library) is installed on the live board and released
(https://github.com/issinoho/vms-php/releases/tag/v8.5.12-vms2). This repository is public
since the same day (https://github.com/issinoho/vms-sqlite), its history scrubbed of node
details first (an account name and UIC group numbers in D5/PHASE2, replaced by placeholders). The work is in `~/projects/vms-php` (its rules and records
apply there: its D28 for the extensions, its PORTING_LOG); this file keeps the SQLite side.

## Resume here (hand-off, 2026-10-23)

**Where things stand.** Phases 0-3 are done and the plan's acceptance is met on x86-64. D0-D10
are approved or chosen by the user. All three repositories are pushed and in sync with GitHub:
vms-sqlite (public since 2026-10-23, no release yet), vms-php (v8.5.12-vms2 released, with this
library), vms-lighttpd (test-tool options only). The website (vms-grep) shows PHP vms2.

**Next: the plan's Phases 4-5, this project's own kit and release** (stop and ask at each exit):
1. Phase 4, VSI C variants: the x86 VSI C variant is already built and tested (Phase 2,
   PHASE2.md: corpus, mptest, KILLTEST, VFS_TEST, CONCURRENT, speedtest1 all pass). IA64 is on
   hold (D2): confirm with the user that Phase 4 is therefore done for x86.
2. Phase 5, the kit (D3: PCSI product `VMSSQLITE`, producer ISSINOHO, version `V3.54-1E1`,
   kit name `ISSINOHO-X86VMS-VMSSQLITE-V0354-1E1-1`, plan line ~48). Nothing exists yet: no
   `tools/kit.sh`, no `MAKE_KIT.COM`, no PCSI description. Model them on vms-zlib's or vms-php's
   (`tools/kit.sh`, `overlay/vmsport/kit/make_kit.com`, `*.pcsi$desc_template`,
   `*.pcsi$text_template`, `readme.vms`, `news`, a `$STARTUP`/`$SETUP` pair defining a root
   logical and symbols). Decide with the user what goes in: the shell `SQLITE3.EXE`, the
   shareable image `SQLITE3$SHR.EXE` (symbol vector from `sqlite3_shr.symbols`, GSMATCH
   LEQUAL 3,54), the object library and headers for developers, both compilers or clang only,
   the docs (COPYING, a README.VMS: the lock file, no WAL, access by UIC protection,
   `VMSSQLITE_TRACE`/`_ERRORS`).
3. The install check on the node, with the user's approval (it installs system-wide).
4. The release (GitHub `v3.54.1-vms1`, kit + `SHA256SUMS`), a banner for the README like the
   family's (`docs/images/banner.svg`), the website card (vms-grep `site/projects.json`, then
   `tools/site-releases.sh`, the README table) and the upstream watch (vms-grep
   `tools/upstream-watch.json`; release-monitoring.org's SQLite project).

**State on the node** (x86; details in nodes.conf, never in commits):
- This work directory: the staged tree `[.SQLITE-3_54_1]`, built from `main` with both
  compilers (the last builds of 2026-10-22). `[.XGROUP]` is empty with no ACL. `[.PERF]`,
  `[.TRACE]`, `[.ERRS]` hold only scratch from tests.
- The shared dependency directory (nodes.conf column 8) has this project's clang install tree
  `[.SQLITE-3_54_1.INSTALL_X86_64_CLANG]` (D10): rerun `tools/deploy.sh x86` after any build
  that should reach PHP.
- **Live:** PHP V8.5-12E2 (vms2) is installed system-wide (`SYS$COMMON:[VMSPHP]`) and the user's
  phpBB board (vms-lighttpd service, pool LTPHP_19000-19003, account LIGHTTPD) runs on it: the
  user checked the pages (2026-10-23). Never touch its files or processes. Rollback: install
  the V8.5-12E1 kit (vms-php `out/kits/`), then `@VMSLIGHTTPD$ROOT:[COM]VMSLIGHTTPD$CONTROL
  RESTART`.
- vms-php's work directory holds the SQLite test board (`[.PHPBB6]`, `[.PHPBB6DATA]`, `[.T6]`,
  `[.T6POOL]`; vms-php D29): stopped, and its GROUP logical `VMSPHP2$ROOT` removed. Bring it
  back with vms-php's `tools/p6.sh x86 logical`, `poolstart`, `start`; `test`, `soak`; then
  `stop`, `poolstop`, `nological`. Its admin password is in vms-php's git-ignored
  `cache/phpbb6.secrets`.

**Open points** (none blocks the kit):
- RMS$_FLK at an open (#26): retried, but which accessor holds the file is not known.
- threadtest4 (shared cache) ends some runs on `SQLITE_LOCKED`, in the test's own retries;
  every completed run has 0 errors (#23). Not a gate.
- mptest's harness reads clients' totals with a 1 s busy limit: a run can report only the
  controller's tests; such a run proves nothing.
- On the SQLite board the first topic post took 19.8 s (later operations are fast); not
  investigated.
- vms-php: its PCSI configuration text still names only PHP's and Argon2's licences (the kit's
  [DOC] has all of them); for its next kit. pdo_mysql's `.phpt` set not run (needs a MySQL
  server, vms-php D14).
- The performance gap to the host is the platform's per-call cost (speedtest1 64.9 s vs 4.5 s;
  1.33 ms per autocommit SELECT, D8).

**Tools added for hand-off:** `tools/suite.sh` (the whole regression run), `tools/procs.sh`
(our processes on the node), `TEST/SPEEDTEST.COM`. The probes in `probes/` have each a header
saying what they showed.

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
