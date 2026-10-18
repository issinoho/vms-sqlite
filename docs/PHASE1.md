# Phase 1: the build

**Status:** done (2026-10-18), awaiting the user's review.

## What is built

`tools/prepare.sh && tools/build.sh x86 ALL CLANG` (or `CC`) builds, from the 3.54.1
amalgamation, two patches and our VFS:

| Product | |
|---|---|
| `LIBSQLITE3.OLB` | `sqlite3.c` + `vmsport/os_vms.c`, the VMS VFS (D5), `SQLITE_OS_OTHER` |
| `SQLITE3.EXE` | the shell, linked with the library |
| `SQLITE3$SHR.EXE` | the shareable image: 277 symbols (`vmsport/sqlite3_shr.symbols`, append-only), `GSMATCH=LEQUAL,3,54` |
| install tree | `[.INSTALL_X86_64_CLANG]` (clang) or `[.INSTALL_X86_64]` (VSI C): `[INCLUDE]SQLITE3.H, SQLITE3EXT.H`, `[LIB]LIBSQLITE3.OLB, SQLITE3$SHR.EXE`, `[BIN]SQLITE3.EXE` |

Both compilers build without a warning in our code (VSI C: three informationals in
upstream's `shell.c`). Compile options: D4. `tools/gen_shr_opt.sh` (run by prepare)
refuses a release whose exports are not all in the symbol list.

- **Patch 0001:** pthread mutexes on `__VMS` under `SQLITE_OS_OTHER`.
- **Patch 0002:** the shell writes its output as one `fwrite` element. The C RTL otherwise
  wrote every character as a record (PORTING_LOG #5).

## The VFS (`overlay/vmsport/os_vms.c`)

- **I/O:** RMS opens the file with UFO; `$QIO` virtual-block reads and writes, straight
  from SQLite's buffers (64-bit addresses work). Partial blocks are read-modify-written;
  a gap written past the end is zeroed.
- **End of file:** kept in the file header. It is read when a read transaction starts and
  when SQLite asks for the size, and written by `xSync`, by `xTruncate`, and before a write
  lock is released. Truncation also gives back the allocation when no other accessor
  prevents it.
- **Locks:** `fcntl()` on `<database>-lock` in exact pairs, one record per lock file per
  process, as the unix VFS does. The lock file's bytes are spaced out: PENDING 0,
  RESERVED 2, SHARED 4-513 (#7).
- **Names:** Unix-style names are converted by the C RTL (`decc$to_vms`), then `$PARSE`
  fills in the rest. `xDelete` removes every version. Temporary files have no directory
  entry and are deleted when closed (`TMP`/`TMD`).
- **Not yet:** WAL's shared memory (methods version 1) and loadable extensions.

## Tests (x86-64, both compilers)

| Test | clang | VSI C |
|---|---|---|
| `test/smoke.com`: 20,000 rows inserted, updated, deleted, indexed; `integrity_check`; read-only reopen with a Unix-style relative name; the shell linked against `SQLITE3$SHR` | same output as the host shell | same |
| `test/vfs_test.c`: 3,000 random reads/writes/truncates against a model (partial blocks, gaps, short reads), reopen, names, `xAccess`, deleting all versions, temp file | 0 failures | 0 failures |
| P0 growth over 5,000 transactions: reads, writes with DELETE and TRUNCATE journals, EXCLUSIVE locking | 0 bytes each | 0 bytes each |
| `test/concurrent.com`: 4 processes × 200 write transactions while a fifth reads 400 times | 4,000 rows, no torn snapshot, `integrity_check` ok (22 s) | same |
| `concurrent.com MIXED`: clang and VSI C processes on one database | 4,000 rows, ok | |

## Open items

- **The cross-group lock test** (D5): one worker under an account in another UIC group.
- **WAL** (shared memory), durability under a killed writer, the SQL corpus against the host,
  `mptest`/`threadtest` and `speedtest1`: Phase 2.
- **The kit's naming of the two variants** (both builds produce `SQLITE3$SHR.EXE`): Phase 5.
