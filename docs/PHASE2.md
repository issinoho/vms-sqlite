# Phase 2: correctness and concurrency

**Status:** in progress (2026-10-18), paused by the user (usage). x86-64, clang build.

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
