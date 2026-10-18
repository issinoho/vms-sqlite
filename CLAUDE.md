# CLAUDE.md

SQLite for OpenVMS (x86-64 first, then IA64), built natively with VSI's clang and VSI C and
wrapped by the same tooling as `~/projects/vms-php`, `vms-zlib`, `vms-pcre2` and the other
sibling ports. Read vms-grep's `CLAUDE.md` for the ground rules and the VMS/ssh pitfalls, and
vms-mariadb's and vms-php's `CLAUDE.md` for the clang and C RTL ones; they all apply. Read
the plan (`docs/SQLITE_OPENVMS_PLAN.md`) and `docs/DECISIONS.md` before starting work. Work
phase by phase; stop and ask at each phase exit.

**Acceptance:** `sqlite3` and the libraries on x86-64 (clang and VSI C), several processes
safely sharing one database, and PHP for OpenVMS's `sqlite3`/`pdo_sqlite` running a phpBB
board on SQLite with no crash under load.

## Ground rules (from the sibling ports)

- **Never edit `staging/`, `cache/` or `out/`.** Every build starts from the release pinned
  in `upstream.conf` by its SHA3-256 (SQLite does not sign its releases). Upstream files change
  only through `patches/` (listed in `patches/series`); our own files live in `overlay/`,
  which may only add files.
- **Keep VMS changes minimal and upstreamable:** guard with `#ifdef __VMS`, one fix per patch,
  each with a `Subject:` line and the VMS reason.
- **Use `tools/vms.sh`** for remote work; never raw `ssh host cmd`, never `WAIT` over ssh.
  Run long jobs (builds, the corpus) with `run_in_background`. A probe's subprocess dies with
  the ssh session: the foreground side must wait for it before the procedure ends.
- **Committed files must not contain real node details** (they live in the git-ignored
  `tools/nodes.conf`), nor credentials. Use `<x86-host>`, `<ia64-host>`.
- **Nothing destructive or system-wide on the nodes** without asking: no DELETE/PURGE outside
  our work directories, no installs, no system logical names. The live board (vms-lighttpd
  service on x86) is the user's: read its logs, never touch its files or processes.
- Every build or run failure gets one entry in `docs/PORTING_LOG.md`; every decision goes in
  `docs/DECISIONS.md` with the alternatives considered.
- Prefer a 20-line probe in `probes/` over speculation. Do not claim something works until a
  test on the node shows it.

## Platform facts that shape the port (details in docs/DECISIONS.md, docs/PHASE0.md)

- **The C RTL's file I/O is not coherent between processes or descriptors:** each descriptor
  buffers what it has written, `fstat` lags, a `pread` past EOF moves EOF (p_mp, p_misc;
  vms-mariadb D6/D10). SQLite's unix VFS can't share a database between processes on it.
- **`$QIO` virtual-block I/O on a UFO channel is coherent**, and the end of file can be read
  and set through the file header's record attributes (p_qio).
- `fcntl()` byte-range locks work between processes but leak C RTL P0 memory on unlock
  (p_leak; vms-php #53). `mmap(MAP_SHARED)` of a file works between processes (p_mp T5).
- clang doesn't read text libraries: starlet headers outside its include path (e.g.
  `fatdef.h`) are extracted from `SYS$LIBRARY:SYS$LIB_C.TLB`; VSI C needs that library named.
- The C RTL feature logicals change clang's own behaviour: define them after compiling.
- `time_t` is 32-bit; VSI C is ILP32, clang LP64.

## Commands

```sh
tools/fetch.sh                                   # download + verify (SHA3-256) into cache/
tools/vms.sh <node> dcl '<cmd>' ...              # run DCL; also run/batch/put/get
tools/vms.sh x86 dcl '@[.probe]vms_probe CLANG FEATURES [probes]'   # Phase 0 probes
```

## Commits

Commit in logical steps with messages that explain the VMS reason for each change. Don't push
without the user asking.
