$! VMSSQLITE$STARTUP.COM - SQLite for OpenVMS at system startup (vms-sqlite D11)
$!
$! Usage (as SYSTEM):
$!   @SYS$STARTUP:VMSSQLITE$STARTUP            from SYSTARTUP_VMS.COM: define
$!                                             VMSSQLITE$ROOT and SQLITE3$SHR
$!   @SYS$STARTUP:VMSSQLITE$STARTUP INSTALL    the same, and print the
$!                                             post-installation tasks (PCSI)
$!   @SYS$STARTUP:VMSSQLITE$STARTUP REMOVE     deassign both (PCSI, at removal)
$!
$! SQLITE3$SHR is the name programs linked against the shareable image find it
$! by at run time; it points at VMSSQLITE$ROOT:[LIB]SQLITE3$SHR.EXE.  REMOVE
$! deassigns it only if it still points there.
$!
$ set noon
$ say = "write sys$output"
$ op = f$edit(p1, "UPCASE,TRIM")
$ shr = "VMSSQLITE$ROOT:[LIB]SQLITE3$SHR.EXE"
$ if op .eqs. "REMOVE" then goto remove
$! This procedure sits in <destination>[SYS$STARTUP]; the product is in
$! <destination>[VMSSQLITE]: DKA0:[SYS0.SYSCOMMON.SYS$STARTUP] -> DKA0:[SYS0.SYSCOMMON.VMSSQLITE.]
$ proc = f$environment("PROCEDURE")
$ dev = f$getdvi(f$parse(proc,,,"DEVICE","NO_CONCEAL"), "FULLDEVNAM")
$ dir = f$edit(f$parse(proc,,,"DIRECTORY","NO_CONCEAL"), "UPCASE") - "]["
$ root = dir - "SYS$STARTUP]" + "VMSSQLITE.]"
$ if root .eqs. dir + "VMSSQLITE.]"
$ then
$   write sys$error "VMSSQLITE$STARTUP: expected to be in a [SYS$STARTUP] directory, not ''dir'"
$   exit 44
$ endif
$ root = dev + (root - ".000000")
$ define/system/executive_mode/translation_attributes=concealed VMSSQLITE$ROOT 'root'
$ old = f$trnlnm("SQLITE3$SHR", "LNM$SYSTEM_TABLE")
$ if old .nes. "" .and. f$edit(old, "UPCASE") .nes. shr then -
     say "VMSSQLITE$STARTUP: SQLITE3$SHR was ", old, "; now ", shr
$ define/system/executive_mode SQLITE3$SHR 'shr'
$ if f$search(shr) .eqs. "" .or. f$search("VMSSQLITE$ROOT:[BIN]SQLITE3.EXE") .eqs. ""
$ then
$   write sys$error "VMSSQLITE$STARTUP: no SQLITE3$SHR.EXE or SQLITE3.EXE under ''root'"
$   exit 44
$ endif
$ if op .nes. "INSTALL" then exit 1
$ say ""
$ say "    Post-installation tasks for SQLite for OpenVMS (vms-sqlite)"
$ say ""
$ say "    VMSSQLITE$ROOT and SQLITE3$SHR are defined now (system logical names)."
$ say "    At boot, add to SYS$MANAGER:SYSTARTUP_VMS.COM:"
$ say "    $ @SYS$STARTUP:VMSSQLITE$STARTUP.COM"
$ say "    The DCL symbol sqlite3, from LOGIN.COM or SYLOGIN.COM:"
$ say "    $ @VMSSQLITE$ROOT:[COM]VMSSQLITE$SETUP"
$ say "    See VMSSQLITE$ROOT:[DOC]README.VMS."
$ say ""
$ say "    PRODUCT REMOVE VMSSQLITE deassigns VMSSQLITE$ROOT and SQLITE3$SHR."
$ say ""
$ exit 1
$!
$remove:
$ if f$edit(f$trnlnm("SQLITE3$SHR", "LNM$SYSTEM_TABLE"), "UPCASE") .eqs. shr then -
     deassign/system/executive_mode SQLITE3$SHR
$ if f$trnlnm("VMSSQLITE$ROOT", "LNM$SYSTEM_TABLE") .nes. "" then -
     deassign/system/executive_mode VMSSQLITE$ROOT
$ exit 1
