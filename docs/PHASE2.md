# Phase 2: correctness and concurrency

**Status:** in progress (2026-10-18), paused by the user (usage). x86-64, clang build.

## Resume here

State at the pause (commit c839e63 and the hand-off commit after it):
- D0-D7 approved; Phases 0 and 1 done (`PHASE0.md`, `PHASE1.md`); nothing pushed and no
  GitHub repository yet (create/push only when the user asks).
- The node's work directory is `[.VMS_SQLITE]` (nodes.conf); the staged tree there is
  `[.SQLITE-3_54_1]`, built with clang and with VSI C at the last build of each (the VSI C
  build predates the block cache and patch 0005: rebuild it first, `tools/build.sh x86 ALL CC`).
- The x86 node also runs the user's live board (lighttpd, MariaDB, PHP pool): read-only for us.
- Next steps, in order:
  1. **speedtest1**: compare `docs/speedtest1-vms-clang.txt` with `docs/speedtest1-host.txt`
     per test; trace one slow test (`VMSSQLITE_TRACE`) for its `$QIO` counts; candidates are
     more cache windows, bigger read-ahead, fewer ACP calls (EOF reads at every transaction).
  2. **VSI C variant**: rebuild, then CORPUS, MPTEST, KILLTEST, VFS_TEST, CONCURRENT with `CC`.
  3. **WAL**: implement xShm* (mmap MAP_SHARED of `-shm` works, p_mp T5; locks on spaced
     bytes of the lock file; the block cache must not be used for a WAL-mode database), or
     decide `SQLITE_OMIT_WAL` with the user (D4).
  4. **Cross-group lock test** (D5 open point): needs the user to run one worker under an
     unprivileged account in another UIC group.
  5. Phase 2 report, then stop for the user's review before Phase 3 (PHP).

## Done (clang)

| Test | Result |
|---|---|
| `mptest` (patches 0003, 0004): config01, config02, crash01, multiwrite01 | 962 tests, 0 errors |
| SQL corpus, 14 scripts, against the host (`tools/corpus.sh`) | identical but for `acosh(2)`'s last bit (libm; `corpus/known-differences.txt`) |
| `KILLTEST.COM`: a writer stopped with STOP/ID at random moments | 20/20 consistent with `synchronous=NORMAL`, 20/20 with `FULL`; hot journals rolled back |
| `threadtest4` (10 threads, shared cache): default, `-serialized`, `-multithread` | 3 × 1,533 tests, 0 errors |
| `vfs_test`, `concurrent.com` (4 writers + reader) | 0 failures, no P0 growth; 4,000 rows, no torn snapshot (11 s) |
| `speedtest1 --size 25` | 182.4 s on VMS against 4.5 s on the host (`docs/speedtest1-*.txt`) |

## VFS changes in this phase (PORTING_LOG #13-#18)

- A cache per file (16 × 32 KB windows, valid/dirty bits per block): `$QIO` costs ~0.3 ms to
  read and ~1 ms to write on this node.
- An exit handler writes buffered data and the end of file at image exit (SQLite relies on a
  dead process's writes surviving, as in the Unix page cache). STOP/ID remains a power failure.
- `VMSSQLITE_TRACE`: per-process lock and I/O trace.
- Patch 0005: the SQL `sqrt` is correctly rounded (the C RTL's is one unit off).

## To do

- `speedtest1`: look at which tests dominate the 40x (per-test times are in the files).
- The VSI C variant: corpus, mptest, kill test.
- WAL (shared memory), or `SQLITE_OMIT_WAL` (D4).
- The cross-group lock test (D5), with the user.
