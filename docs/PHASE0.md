# Phase 0: reconnaissance and probes

**Status:** done (2026-10-18), awaiting the user's review of D0-D7.

x86-64 node, OpenVMS E9.2-4, ODS-5 work disk; clang 10.0.1 (VSI build) and VSI C V7.7-003.
Raw output: `docs/probes-x86-clang.txt`, `docs/probes-x86-cc.txt` (the same results with
both compilers). Probes are in `probes/`, run by `probes/vms_probe.com` with the C RTL
feature logicals SQLite would set (`DECC$FILE_SHARING`, `DECC$ALLOW_REMOVE_OPEN_FILES`,
`DECC$EFS_CHARSET`, ...).

## The release

SQLite 3.54.1, `sqlite-autoconf-3540100.tar.gz` and `sqlite-src-3540100.zip`, SHA3-256
checked by `tools/fetch.sh` (D1). Host reference: `sqlite3` built with the same options
(gcc, Linux).

## As it is: SQLite compiles and runs single-process

`probes/recon.com` built `sqlite3.c` and `shell.c` unchanged with SQLite's unix VFS:
- **clang:** no diagnostics in `sqlite3.c` (~90 s at `-O2`); `shell.c` needs
  `-pointer-size=argv64` (`main`'s `argv` is 32-bit otherwise).
- **VSI C:** no diagnostics in `sqlite3.c` (~100 s); `shell.c` informationals only.
- Both shells gave the host's results for `probes/recon_sql.txt`: `SUM`/`AVG`/`TOTAL` over
  integers, reals and NULLs, `printf`, dates, JSON, window functions, a 100,000-row recursive
  CTE, `integrity_check` (D7: no sign of the VSI C aggregate bug).
- The shell writes **one character per record** when its output goes to a file (Phase 1).

## Two processes, one file: the C RTL is not coherent (p_mp)

| Check | Result |
|---|---|
| T0 second process opens the file `O_RDWR` | OK |
| T1 locks near 1 GB on a small file, between processes (also unprivileged) | OK |
| T2 read the other process's write after its fsync | **BAD**, both directions: each reads its own old copy |
| T3 `fstat` size after the other extended the file | OK (`lseek(SEEK_END)` stale) |
| T3b read the page the other appended | **BAD**: `pread` returns 0 |
| T4 size after the other's `ftruncate` | OK |
| T5 `mmap(MAP_SHARED)` of one file, stores seen by the other | OK |

## One process (p_misc)

`fstat` is 0 for a file being written until fsync; a `pread` past EOF moves EOF at close
(4096 became 100000); `O_TRUNC` creates a second version; `open(".")` and `realpath()` fail;
`-journal`/`-wal`/two-dot names are fine; `getcwd` is a Unix path; `time_t` is 32-bit.

## Lock leaks (p_leak, 5000 cycles)

| Pattern | P0 per cycle |
|---|---|
| SQLite's (convert in place, unlock 2 locks at once, unlock the whole file) | 1086 bytes |
| same, ending with an exact SHARED unlock | 616 |
| exact lock/unlock pairs, 510 bytes or 1 byte | 0 |
| read lock converted to write in place, exact unlock | 161 |

At ~1 KB a transaction, a PHP worker would lose its page-file quota in hours, as with
opcache before vms-php patch 0020.

## Lock namespace (p_lkns)

The C RTL's `fcntl()` locks are system-wide lock-manager resources `C$__<device><fid>`. Our
own `$ENQ` with `LCK$M_SYSTEM` fails without SYSLCK (`%X28F4`), and without that flag a
lock lives in the UIC group's namespace. Unprivileged C RTL locks still excluded each other
(p_mp T1). **Not yet tested:** two accounts in different groups.

## `$QIO` block I/O (p_qio): coherent

RMS open with UFO (shared `GET/PUT/UPI`), `IO$_READVBLK`/`IO$_WRITEVBLK`, EOF as the header's
record attributes:

| Check | Result |
|---|---|
| Q0 extend, set EOF, the other process sees it | OK |
| Q1 writes seen both ways (read-modify-write of a block) | OK |
| Q2 the other's extend: size and data | OK |
| Q3 the other's shrink | OK |
| Q4 C RTL `stat()` after both close | OK |
| Q5 4 KB transfers (CPU time) | ~3,600 reads/s, 2,000-3,500 writes/s |
| Q6 a C RTL descriptor kept open for locks only, closed | **BAD**: writes its stale EOF (8192 over 4096); `fac=get` with `O_RDWR` is refused |

## Conclusions (docs/DECISIONS.md)

- **D5:** SQLite needs its own VFS on VMS: `$QIO` I/O with EOF in the file header, and locks
  through the C RTL's `fcntl()` on a companion `<db>-lock` file in exact pairs (no leak, no
  EOF damage). Alternative: `$ENQ` locks, which are only safe with SYSLCK or within one UIC
  group.
- **D4:** WAL is possible (shared mmap works); mmap I/O off; extensions off.
- **D7:** no VSI C aggregate bug seen with V7.7-003.
- IA64 not probed (PHP for IA64 is on hold).

## Open questions for the user

1. Approve D0-D7, in particular D5 (our own VFS) and its lock option (a).
2. The cross-group lock test: one side of `p_mp` run under a second account in another UIC
   group (for example the LIGHTTPD account), started by the user.
