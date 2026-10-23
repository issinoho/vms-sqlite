# Phase 5: the kit and release (x86-64)

**Status:** done (2026-10-23): the kit passed its install check and is released
(https://github.com/issinoho/vms-sqlite/releases/tag/v3.54.1-vms1; the downloaded kit matches
`SHA256SUMS`). Phase 4 is done for x86-64 (the user, 2026-10-23): the VSI C variant was built
and tested in Phase 2 (PHASE2.md), and IA64 is on hold (D2).

## Resume here

1. The kit: done. `tools/kit.sh x86` builds the clang variant and makes
   `out/kits/ISSINOHO-X86VMS-VMSSQLITE-V0354-1E1-1.PCSI` (D3, D11); `tools/installcheck.sh x86`
   (asks the user first: it installs system-wide) installs it, tests it and removes it.
2. The release: done. `v3.54.1-vms1` (the kit and `SHA256SUMS`; notes made from
   `overlay/vmsport/kit/news` and this file), the README's banner (`docs/images/banner.svg`) and
   install section; in vms-grep the website card (`site/projects.json`, `site/releases.json`),
   the README table and the upstream watch (release-monitoring.org project 4877). The user
   pushes and creates releases (the harness refuses `git push` from here).
3. Open: PHASE3.md's open points; IA64 (D2, on hold); a VSI C (ILP32) build in a later kit if
   32-bit-pointer callers appear (D11). The next release follows SQLite's (D1): a new `vms<n>`,
   new exports appended to `sqlite3_shr.symbols`.

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
