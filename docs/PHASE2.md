# Phase 2: correctness and concurrency

**Status:** done (2026-10-22), for the user's review before Phase 3 (PHP). x86-64, clang
and VSI C. Nothing pushed, no GitHub repository yet (create/push only when the user asks).

## Resume here

- D0-D9 approved (D8 on 2026-10-22, after this report). Phases 0 and 1 done (`PHASE0.md`,
  `PHASE1.md`); Phase 2 reviewed by the user, Phase 3 started (`PHASE3.md`).
- The node's work directory is `[.VMS_SQLITE]` (nodes.conf); the staged tree there is
  `[.SQLITE-3_54_1]`, built from the current tree with both compilers. `[.XGROUP]` is
  empty and has no ACL (CROSSGROUP's shared directory).
- Never run two test jobs in one directory at once (`[.PERF]`, `[.TEST.RUN]`), nor a
  timing-sensitive one (mptest) beside anything else: after a `vms.sh` timeout the remote
  job goes on; list our processes and stop them (#17, #23).
- The x86 node also runs the user's live board (lighttpd, MariaDB, PHP pool): read-only for us.
- Next: the user's review, then Phase 3 (PHP's `sqlite3` and `pdo_sqlite`, the phpBB board).

## Results (both compilers; the final build, 2026-10-22, unless dated)

| Test | clang | VSI C |
|---|---|---|
| `vfs_test`: random I/O against a model, names, 10 `xAccess` cases, kept lock files, P0 growth over 4 × 5,000 transactions | 0 failures, 0 bytes | 0 failures, 0 bytes |
| `mptest` (patches 0003, 0004): config01, config02, crash01, multiwrite01 | 965 tests, 0 errors | 965 tests, 0 errors |
| `mptest config02` repeated (hot-journal recovery after a client dies; 512-byte pages with `auto_vacuum`) | 10 runs: 0 errors, 0 timeouts | 10 runs: 0 errors, 0 timeouts |
| `KILLTEST.COM`: a writer stopped with STOP/ID at random moments, 20 kills | 20 consistent, hot journals rolled back | 20 consistent |
| `CONCURRENT.COM`: 4 writers (200 transactions of 5 rows each) and a reader | 4,000 rows, no torn snapshot | 4,000 rows; also 40 runs after #26's fix (2026-10-21) |
| `CONCURRENT MIXED`: clang and VSI C images on one database (2 runs, 2026-10-21) | 4,000 rows | |
| SQL corpus, 14 scripts, compared with the host | identical (but `acosh(2)`'s last bit, libm) | identical |
| `threadtest4` (10 threads, shared cache, kernel threads; 9 runs each, 2026-10-21) | every completed run 0 errors in 1,533 tests; 2 runs ended on `SQLITE_LOCKED` (below) | the same; 4 runs ended on `SQLITE_LOCKED` |
| Cross-group (D5): `CROSSGROUP.COM`, UIC groups <group-a> and <group-b> | locks exclude both ways; 2 writers per account at once: 4,000 rows, `integrity_check` ok | |
| `speedtest1 --size 25` (host 4.5 s) | 64.9 s | 66.8 s |

## Bugs found and fixed in this phase

| # | What | Since | Fix |
|---|---|---|---|
| 25 | **Hot journals overwritten**: after a writer died, the others all tested the RESERVED byte at once by taking and releasing it; one's test made another's fail, read as "a live writer", so a process overwrote the dead writer's journal with its own and the crashed transaction's pages stayed in the database. mptest config02 corrupted ~1 run in 6-13 | Phase 1 | `F_GETLK`, which takes no lock (as the unix VFS) |
| 19 | The change counter alone does not prove a file unchanged: a hot-journal rollback restores it while truncating the file | D8 (1), caught before commit | the header's page count × page size must match too |
| 26 | `RMS$_FLK` at a worker's open, ~1 run in 20 (the other accessor not found: p_cif's 3,600 concurrent opens did not meet it) | Phase 1 | retry the open on FLK for up to 2 s |
| 27 | Contended 1-3 MB write transactions took 1-2 s (cache thrashing, a `$QIO` per dirty run, 32 KB extensions with 3 ACP calls): mptest's 10 s waits ran out | Phase 1 | cache up to 64 windows, one `$QIO` per window, files grow by a quarter |
| 21, 22 | VSI C: `sqrt` one unit off (25% of inputs); no `<sched.h>` | Phase 1 | patch 0005 extended (an exact quad-precision check), patch 0006 |
| 14, 16 | A dead process's buffered writes lost; slow transactions | early Phase 2 | exit handler; the block cache |

Earlier runs that recorded config02 and threadtest4 as passing were lucky: both fail now and
then with the code as it was then (#23, #25). The results above come from repeated runs.

## The VFS as built (`overlay/vmsport/os_vms.c`; design D5, costs D8, no WAL D9)

- **I/O:** RMS opens the file with UFO; `$QIO` virtual-block reads and writes on the channel;
  the end of file is the header's record attributes, through the ACP. An open meeting
  `RMS$_FLK` is retried.
- **Cache per file:** up to 64 windows of 32 KB, allocated as needed, with valid and dirty
  bits per block; kept across transactions while block 1 shows the same change counter
  and size; dirty windows go out (one `$QIO` each) at sync, truncate, close, lock release,
  eviction and image exit.
- **Locks:** C RTL `fcntl()` locks on `<db>-lock`, at spaced bytes, every lock released with
  exactly its range (no P0 leak); RESERVED tested with `F_GETLK`; one record per lock file
  per process, kept after the last connection while an ACP lookup finds the same file id.
  They exclude across UIC groups (D5's test).
- **Costs:** an ACP call ~0.62 ms, a `$QIO` read 0.28 ms and write ~1 ms, an fcntl pair
  0.16 ms (p_cost). Per read transaction: no ACP read of the end of file, `xAccess` of the
  journal by one ACP lookup, no `-wal` check: **1.33 ms per autocommit SELECT** (4.3 ms
  before D8; the host 8 µs). Opening a database and running a few statements: 23 ms (52 ms).
- **No WAL** (D9): the C RTL's shared mapping is per UIC group.
- **Diagnostics:** `VMSSQLITE_TRACE` (every lock, sync, open and end-of-file decision per
  process) and `VMSSQLITE_ERRORS` (errors only, timing untouched).

Patches: 0001-0004 as in Phase 1; 0005 `sqrt` correctly rounded (both compilers); 0006
threadtest4 without `<sched.h>` (VSI C).

## Known limits

- **Speed:** speedtest1 is ~15× the host. What is left is the platform's per-call cost
  (fcntl, ACP lookup, `$QIO` ~0.3-1 ms, where Linux answers from memory). Under heavy write
  contention a 1-3 MB transaction still takes ~0.7-1.2 s.
- **threadtest4** (shared cache): runs sometimes end on `SQLITE_LOCKED` from shared-cache
  table locks, which the test retries only 10 times; no VFS call takes part, and the code as
  it was in Phase 1 does the same. Every run that completes has 0 errors (#23). Not a gate.
- **mptest's harness** reads the clients' totals with a 1 s busy limit: a run can report
  only the controller's tests (once with VSI C: "0 errors out of 46"); such a run proves
  nothing either way.
- **`RMS$_FLK`** (#26): retried, but its source is not known.
- **IA64:** not started (D2). **No loadable extensions** (D4).

## Tools added in this phase

`tools/corpus.sh`; `TEST/` procedures `MPTEST`, `KILLTEST`, `CONCURRENT` (now checks every
worker's rows), `CROSSGROUP` (with `XGLOCK.EXE`), `VFS_TEST`, `PROGS` (test programs relinked
at every build); probes `p_cost`, `p_acclk`, `p_sqrt`, `p_yield`, `p_shm`, `p_mmoff`,
`p_mmgrow`, `p_exitlk`, `p_getlk`, `p_cif`. PORTING_LOG #13-#27.
