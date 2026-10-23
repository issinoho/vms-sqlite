# Phase 5: the kit and release (x86-64)

**Status:** in progress (2026-10-23): the kit is built and its install check passes; the
release is next. Phase 4 is done for x86-64 (the user, 2026-10-23): the VSI C variant was built
and tested in Phase 2 (PHASE2.md), and IA64 is on hold (D2).

## Resume here

1. The kit: done. `tools/kit.sh x86` builds the clang variant and makes
   `out/kits/ISSINOHO-X86VMS-VMSSQLITE-V0354-1E1-1.PCSI` (D3, D11); `tools/installcheck.sh x86`
   (asks the user first: it installs system-wide) installs it, tests it and removes it.
2. Next, each with the user's go-ahead (public): the GitHub release `v3.54.1-vms1` (the kit
   and `SHA256SUMS`, notes from `overlay/vmsport/kit/news`); a banner for the README
   (`docs/images/banner.svg`, like the family's) and its status line; the website card
   (vms-grep `site/projects.json`, `tools/site-releases.sh`, the README table); the upstream
   watch (vms-grep `tools/upstream-watch.json`, release-monitoring.org's SQLite project).
3. The open points of PHASE3.md's hand-off still stand; none blocks the release.

## The kit (D11)

`VMSSQLITE V3.54-1E1`, clang (LP64) build: `[VMSSQLITE.BIN]SQLITE3.EXE`, `[.LIB]LIBSQLITE3.OLB`,
`SQLITE3$SHR.EXE`, `[.INCLUDE]SQLITE3.H`, `SQLITE3EXT.H`, `[.COM]VMSSQLITE$SETUP.COM`, `[.DOC]`
README.VMS, COPYING, NEWS, SQLITE3.TXT; `SYS$STARTUP:VMSSQLITE$STARTUP.COM` defines
`VMSSQLITE$ROOT` and `SQLITE3$SHR` (system, executive mode). 7,692,288 bytes.

## Results (x86-64, 2026-10-23)

| `tools/installcheck.sh x86` (out/install-x86.txt) | |
|---|---|
| `PRODUCT INSTALL`, the post-install procedure | success; both logicals defined |
| `kit_test.c`, clang (`-I/vmssqlite$root/include`), linked with `LIBSQLITE3.OLB` | PASS: 3.54.1, 1,000 rows written and read back |
| `kit_test.c`, VSI C `/POINTER_SIZE=64/NAMES=AS_IS`, linked with `SQLITE3$SHR/SHAREABLE` | PASS (the clang library from a VSI C program) |
| the shell through `VMSSQLITE$SETUP`'s `sqlite3` symbol | PASS: `1000 500500` |
| `PRODUCT REMOVE` | success; logicals, files and startup procedure gone |

No compiler or linker diagnostics. PHP is unaffected: it links SQLite statically (D10).
