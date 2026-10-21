# Phase 2: correctness and concurrency

**Status:** in progress (2026-10-18), paused by the user (usage). x86-64, clang build.

## Resume here

State at the pause (2026-10-20, after D8 and the VSI C variant):
- D0-D7 approved; D8 (2) and D9 (no WAL) chosen by the user, D8 (1) and (3) as built, for review; Phases 0 and 1 done (`PHASE0.md`, `PHASE1.md`); nothing pushed and no
  GitHub repository yet (create/push only when the user asks).
- The node's work directory is `[.VMS_SQLITE]` (nodes.conf); the staged tree there is
  `[.SQLITE-3_54_1]`, built from the current tree with both clang and VSI C.
- Never run two test jobs in one directory at once (`[.PERF]`, `[.TEST.RUN]`): after a
  `vms.sh` timeout the remote job goes on (#17, #23).
- The x86 node also runs the user's live board (lighttpd, MariaDB, PHP pool): read-only for us.
- Next steps, in order:
  1. **speedtest1** (in progress): the time is per read transaction, not I/O volume (D8).
     Done: no ACP reads of the end of file per read transaction (D8 (1)), `xAccess` by one
     ACP lookup (D8 (2) (b)); 182.4 s -> 132.5 s -> 101.0 s (2.13 ms per autocommit SELECT,
     of which ~1.5 ms the two `xAccess` lookups), then the lock file kept between
     connections (D8 (3)): 98.3 s; test 110 ~30 ms per open-and-use, of which ~17 ms its 8
     read transactions. The rest of speedtest1's gap is per-transaction cost (D8).
  2. **Cross-group lock test** (D5 open point): needs the user to run one worker under an
     unprivileged account in another UIC group.
  3. Phase 2 report, then stop for the user's review before Phase 3 (PHP).

## Done (clang)

| Test | Result |
|---|---|
| `mptest` (patches 0003, 0004): config01, config02, crash01, multiwrite01 | 962 tests, 0 errors |
| SQL corpus, 14 scripts, against the host (`tools/corpus.sh`) | identical but for `acosh(2)`'s last bit (libm; `corpus/known-differences.txt`) |
| `KILLTEST.COM`: a writer stopped with STOP/ID at random moments | 20/20 consistent with `synchronous=NORMAL`, 20/20 with `FULL`; hot journals rolled back |
| `threadtest4` (10 threads, shared cache): default, `-serialized`, `-multithread` | 3 × 1,533 tests, 0 errors |
| `vfs_test`, `concurrent.com` (4 writers + reader) | 0 failures, no P0 growth; 4,000 rows, no torn snapshot (11 s) |
| `speedtest1 --size 25` | 98.3 s on VMS (182.4 s before D8; 132.5 s after D8 (1), 101.0 s after (2)) against 4.5 s on the host (`docs/speedtest1-*.txt`) |
| VSI C (2026-10-20, with D8 (1)-(3)) | corpus identical (patch 0005 extended, #21); mptest 965 tests, 0 errors; KILLTEST 20/20; VFS_TEST, CONCURRENT ok; speedtest1 99.2 s (`docs/speedtest1-vms-cc.txt`) |
| `threadtest4` again, 9 runs per compiler (#23) | every completed run 0 errors in 1,533 tests; some runs end on `SQLITE_LOCKED` from shared-cache locks (the Phase 2 code too): a timing-sensitive test, not a gate. Linked with kernel threads since |
| all of the above again after each of D8 (1), (2), (3) (2026-10-20) | mptest 965 tests, 0 errors (crash01 also 3 more runs); KILLTEST 20/20; corpus identical; VFS_TEST (now with 10 `xAccess` cases), CONCURRENT ok |

## VFS changes in this phase (PORTING_LOG #13-#18)

- A cache per file (16 × 32 KB windows, valid/dirty bits per block): `$QIO` costs ~0.3 ms to
  read and ~1 ms to write on this node.
- An exit handler writes buffered data and the end of file at image exit (SQLite relies on a
  dead process's writes surviving, as in the Unix page cache). STOP/ID remains a power failure.
- `VMSSQLITE_TRACE`: per-process lock and I/O trace.
- Patch 0005: the SQL `sqrt` is correctly rounded (the C RTL's is one unit off).
- The end of file is read once per lock, not at all when block 1 shows the same counter and
  size (D8 (1), PORTING_LOG #19: the counter alone is wrong after a hot-journal rollback).
- `probes/p_cost.c`: what each primitive costs (ACP ~0.62 ms, `$QIO` read 0.28 ms, fcntl pair
  0.16 ms, failed `sys$open` 1.15 ms); `VMSSQLITE_TRACE` also logs end-of-file decisions.
- `xAccess(EXISTS)` by one ACP lookup for plain names, the RMS open for ODS-5 extended
  ones (D8 (2) (b), `probes/p_acclk.c`); the trace shows which way each call went.
- The lock file's record is kept after the last connection and used again after one ACP
  lookup confirms the file id (D8 (3)); vfs_test 6 renames a kept one.

## To do

- The cross-group lock test (D5), with the user.
