# Plan: SQLite for OpenVMS (vms-sqlite)

**Status:** Phase 0 done (2026-10-18, `docs/PHASE0.md`); D0-D7 await review. Phase 0 found
that SQLite needs its own VFS on VMS (D5), which Phase 1 builds.

## Context

SQLite is an embedded SQL database: a C library and the `sqlite3` shell, with no server,
account or service. OpenVMS sites could use it on its own, for local state of tools and
applications written in DCL, C or PHP. The main reason for this port, though, is PHP:

- **PHP for OpenVMS** ([vms-php](https://github.com/issinoho/vms-php)) is a clang LP64 build.
  It can only link a SQLite built the same way. With one, the next vms-php release gains
  `sqlite3`, `pdo` and `pdo_sqlite`, and with `pdo` also `pdo_mysql`, the extension phpBB's
  checks flag as missing. VSI's PHP 8.1 had `sqlite3` and `pdo_sqlite`, so sites moving to PHP
  8.5 lose them today.
- **A community port exists** (vms-ports on SourceForge, "kit 50", SQLite 3.47.0, built with
  VSI C). It works around a VSI C compiler bug on x86 that breaks the built-in `SUM()`,
  `AVG()` and `TOTAL()` by reimplementing them as an auto-loaded extension, and has an
  experimental clang build. This port adds:
  - the current release (3.54.1);
  - a clang LP64 library for PHP, which also avoids that compiler bug;
  - PCSI kits in the family's style;
  - the family's test and release tooling.

## Key design decisions (to record as D0-Dn)

1. **D1, release:** SQLite **3.54.1**, `sqlite-autoconf-3540100.tar.gz` (the amalgamation
   `sqlite3.c`, `sqlite3.h`, the shell's `shell.c`, a configure script). SQLite publishes
   SHA3-256 hashes but no signatures, so `upstream.conf` pins the hash from sqlite.org's
   download page. We follow SQLite's releases; each takes a new `vms<n>`.
2. **D2, variants**, as vms-zlib and vms-pcre2 already do:
   - **x86-64 clang (LP64):** the primary variant, for PHP and other clang programs;
   - **x86-64 VSI C (ILP32):** for VSI C programs (and the SSL3 `_SHR32` ABI). It must deal with
     the aggregate-function bug (D7);
   - **IA64 VSI C:** if the VSI C variant works on x86.

   Each variant installs an object library and headers in `<tree>.INSTALL_<arch>[_CLANG]`
   (`[LIB]LIBSQLITE3.OLB`, `[INCLUDE]SQLITE3.H`, `SQLITE3EXT.H`), the layout vms-php's
   `BUILD.COM` already uses for zlib, PCRE2 and libxml2.
3. **D3, the kit:** PCSI product `VMSSQLITE` (VSI ships no SQLite), with:
   - the `sqlite3` shell;
   - the libraries and headers for linking;
   - a shareable image (`SQLITE3$SHR`) for programs that link SQLite at run time;
   - `VMSSQLITE$ROOT` and a setup procedure for the `sqlite3` symbol.

   Version `V3.54-1E1`, kit name `ISSINOHO-X86VMS-VMSSQLITE-V0354-1E1-1`, checked against PCSI's
   39-character limit.
4. **D4, compile-time options**, decided by the probes (Phase 0):
   - `SQLITE_THREADSAFE=1` (pthreads);
   - `SQLITE_DEFAULT_MEMSTATUS=0`;
   - `SQLITE_OMIT_LOAD_EXTENSION` unless VMS shareable images prove workable as SQLite
     extensions;
   - `SQLITE_MAX_MMAP_SIZE=0` unless file mmap proves safe;
   - WAL only if shared-memory mapping of the `-shm` file works across processes, otherwise
     rollback journals only (`SQLITE_OMIT_WAL`, or WAL documented as single-process with
     `locking_mode=EXCLUSIVE`);
   - the temporary directory (`SQLITE_TMPDIR` / `sqlite3_temp_directory`) defaulting to
     `/sys$scratch`;
   - the shell's extras (FTS5, JSON, R*Tree, math functions) on; no readline.
5. **D5, the VFS:** SQLite's `unix` VFS over the C RTL, made correct by small, upstreamable
   patches (`#ifdef __VMS`). The alternative is a VMS VFS of our own, as an overlay file. That
   would only make sense if the probes show the C RTL can't give SQLite the locking and
   durability it needs.
6. **D6, file names and versions:** SQLite creates and deletes `-journal`, `-wal` and `-shm`
   files beside the database. `unlink()` must remove every version (vms-php #37), and the
   database file must keep one version. On ODS-5 the dotted and dashed names are valid.
7. **D7, the VSI C aggregate bug** (x86 VSI C variant only): pin it down with a probe, then
   either use a compiler option that avoids it, or patch the code it miscompiles, or adopt the
   community kit's auto-loaded replacements. The clang variant must show the same results as
   the host.

## Expected VMS work (probes first: `probes/`, each about 20 lines)

SQLite's correctness depends on the file system behaving as POSIX says. Every C RTL quirk
found in vms-php and vms-mariadb is a candidate:

- **fcntl() byte-range locks** across processes: SQLite's locking takes and releases ranges
  of different lengths (PENDING, RESERVED, SHARED). vms-php found that unlocking a range wider
  than the one held leaks C RTL memory each time (#53), and SQLite does exactly that
  (`unixUnlock` to `NO_LOCK`). Probe the leak and whether locks between processes are honoured.
- **Write visibility across descriptors:** vms-mariadb D6 found that writes through one
  descriptor are not seen through another until flushed. SQLite's processes each have their
  own descriptor, and readers must see a committed transaction, so probe write, fsync,
  another process's read.
- **fsync, ftruncate, and the file's size afterwards** (vms-mariadb D10: a stale size after
  truncate).
- **unlink() of versions, and of an open file** (vms-php #37, and opcache's lock file).
- **mmap of a file, `MAP_SHARED`, across processes:** for WAL's `-shm` and for mmap I/O.
- **Temporary files:** there is no `/tmp`.
- **The rest:** `getpid`, `usleep`, `gettimeofday` and `localtime` (32-bit `time_t`), `pread`
  and `pwrite`, `access()` (ignores ACLs: vms-php #61), `realpath` (ENOSYS on VMS:
  `unixFullPathname`).

## Phases (stop and report at each exit)

**Phase 0, reconnaissance and probes** (x86, clang and VSI C):
- pin 3.54.1 and fetch it (`tools/fetch.sh`, SHA3-256);
- build it on the host as the reference;
- run the probes above, in both compilers;
- pin down the aggregate bug on VSI C x86;
- **exit:** `PHASE0.md`, and D1-D7 decided.

**Phase 1, the build:**
- `BUILD.COM` from the amalgamation: the object library, the shell and the shareable image;
  clang x86 first, then VSI C x86;
- `sqlite3 -version`, and a database created, written, read and checked
  (`PRAGMA integrity_check`).

**Phase 2, correctness and concurrency:**
- **An SQL corpus run on the host and on VMS, outputs compared** (like vms-php's `.phpt`
  runner): SQLite's own `test/*.test` need Tcl, which VMS does not have. So the corpus is
  SQL scripts covering types, collation, joins, window functions, JSON, FTS5, R*Tree,
  date/time, math, and the aggregate functions.
- **`mptest`** (SQLite's multi-process test) and **`threadtest`:** several processes and
  threads writing one database; locking correct, no corruption.
- **Durability:** kill a writer mid-transaction (STOP/ID), then `integrity_check` and the
  data from the last committed transaction (as vms-mariadb's crash cycle does).
- **Performance:** `speedtest1` on VMS against the host.
- WAL and mmap tested if D4 enables them.

**Phase 3, PHP:** in vms-php, the next release (8.5.x-vms2):
- link the clang library, and enable `sqlite3`, `pdo`, `pdo_sqlite` and `pdo_mysql`;
- run their `.phpt` sets against the Linux reference;
- install a phpBB 3.3 test board on SQLite (phpBB supports `sqlite3`) and run
  `test_phpbb.py` and a load run. That is the acceptance test.

**Phase 4, VSI C variants:**
- the x86 VSI C variant with D7 resolved, then IA64 if the node allows;
- the corpus and `mptest` on each.

**Phase 5, kit and release:**
- the PCSI kits (x86-64, and IA64 if done);
- the install check, with the user's approval;
- the GitHub release, and the website entry (vms-grep `site/`, the README table, the upstream
  watch: release-monitoring.org's SQLite project).

## Acceptance

- `sqlite3` and the libraries work on x86-64 (clang and VSI C), and IA64 if done.
- The SQL corpus matches the host, apart from documented differences.
- `mptest` and the durability test pass.
- PHP for OpenVMS's `sqlite3` and `pdo_sqlite` tests pass.
- A phpBB board runs on SQLite with no crash under load.

## Repository (as the siblings)

- `upstream.conf`, `keys/` (none: hash-pinned);
- `patches/` with `patches/series`, and `overlay/`, which may only add files;
- `tools/`: `fetch.sh`, `prepare.sh`, `build.sh`, `kit.sh`, `vms.sh`, and the corpus runner;
- `probes/` and `docs/` (`DECISIONS.md`, `PORTING_LOG.md`, `PHASE<n>.md`);
- `CLAUDE.md`, from vms-grep's ground rules, plus vms-php's and vms-mariadb's lessons.

## Risks

- **The locking model.** If fcntl locks between processes are unreliable on VMS, or the C
  RTL's per-unlock leak can't be avoided (SQLite unlocks very often), SQLite needs a VMS VFS:
  locking through the lock manager (`SYS$ENQ`), which is a bigger piece of work (D5).
- **Write visibility between processes.** If a committed write is not visible to other
  processes after fsync, multi-process use needs the VMS VFS too, or must be documented as
  single-writer.
- **WAL.** Without a working shared mapping of the `-shm` file, WAL stays off. Rollback
  journals are slower under concurrent readers, but correct.
- **The VSI C aggregate bug** may hide others. The corpus compares every result with the host
  to catch them.
