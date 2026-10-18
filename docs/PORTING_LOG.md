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
