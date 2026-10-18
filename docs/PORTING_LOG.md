# Porting log

One entry per build or run failure: command, error, root cause, fix, patch.
Triage codes: T toolchain/flags, P probe, H header, L libc function, F file system,
S process model, U upstream bug exposed by a new platform.

| # | Date | Command | Error | Code | Root cause | Fix / patch |
|---|---|---|---|---|---|---|
| 1 | 2026-10-18 | `vms_probe CLANG FEATURES` (first) | `error: unable to open output file 'p_misc.obj': 'unspecified generic_category error'` for every probe | T | the C RTL feature logicals (`DECC$FILENAME_UNIX_REPORT` and others) were defined before compiling, and they change how clang itself opens its output file | the procedure defines them after compiling |
| 2 | 2026-10-18 | `vms_probe CLANG FEATURES p_qio` | `'fatdef.h' file not found` | H | `fatdef.h` is only in `SYS$LIBRARY:SYS$LIB_C.TLB`; clang reads no text libraries | extracted with `LIBRARY/EXTRACT` into the probe directory, `-I.`; VSI C names `SYS$LIB_C.TLB/LIBRARY` |
| 3 | 2026-10-18 | same | `FAT` is a typedef, not `struct fatdef` (incomplete type) | H | the header names the structure `struct _fat` / `FAT` | the probe uses `FAT` |
| 4 | 2026-10-18 | same | A's last lines (Q4) missing from its log, log "locked by another user" | P | A's output was buffered in a subprocess that was deleted when the ssh session ended | A writes unbuffered; B waits 4 s before the procedure ends |
| 5 | 2026-10-18 | `SMOKE CLANG` (first Phase 1 build) | every character of the shell's results on a line of its own | L | `cli_write()` uses `fwrite(z, 1, n, out)`; on a record-oriented file (SYS$OUTPUT, a DCL /OUTPUT file) the C RTL writes each element as a record | patch 0002: one element of n bytes on VMS |
| 6 | 2026-10-18 | `CONCURRENT CLANG 4` (first) | two workers: `Error: database is locked` at start, their rows inserted with `w` NULL | P | the test's `-cmd ".parameter set"` ran before `WORK.SQL` set `.timeout`, so it met a writer and failed at once | `-cmd ".timeout 60000"` first (the database itself was intact: 4000 rows, integrity ok) |
| 7 | 2026-10-18 | `VFS_TEST CLANG` section 5 | P0 grows 898 bytes per write transaction (reads, and `locking_mode=EXCLUSIVE`, 0) | L | every lock call was an exact pair, but a writer holds PENDING, RESERVED and the SHARED range together, and SQLite's bytes are adjacent (0x40000000, +1, +2..+511): the C RTL leaks when one of adjacent held ranges is released | os_vms.c: the lock file's bytes are spaced out (PENDING 0, RESERVED 2, SHARED 4..513); 0 bytes on every path |
| 8 | 2026-10-18 | `build.sh x86 ALL CC` (first VSI C build) | `%CC-E-DECLARATOR` at `int readonly;`; `_malloc32` implicitly declared | H | `readonly` is a VSI C keyword; `_malloc32` is declared only with 64-bit pointers (under ILP32 all memory is 32-bit) | field renamed `isReadonly`; `vmsMalloc32` is `malloc` with 32-bit pointers |
| 9 | 2026-10-18 | `SMOKE CLANG` after #8 | every open: `unable to open database file` (SQLITE_CANTOPEN) | H | #8's test was `__INITIAL_POINTER_SIZE == 64`, which clang does not define, so clang got `malloc` (64-bit) and RMS refused the FAB and NAML there | `_malloc32` for `__clang__` too; `alloc32` returns NULL (logged) for any address above 2 GB |
| 10 | 2026-10-18 | `build.sh x86 ALL CLANG`, shareable shell link | `%DCL-W-INSFPRM` at the `DEFINE`, then `%ILINK-F-OPENIN ... SQLITE3$SHR.EXE` | T | `'f$parse("''obj'SQLITE3$SHR.EXE")'` substituted to an empty value | the name is put in a symbol first |
