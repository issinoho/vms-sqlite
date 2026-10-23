$! VMS_INSTALLCHECK.COM <tree-dir-name> - install the VMSSQLITE kit, verify it,
$! build kit_test.c against the installed kit with clang (object library) and
$! VSI C (64-bit pointers, SQLITE3$SHR), run both and the shell, then remove
$! the kit.  Changes the system while it runs (PCSI database,
$! SYS$COMMON:[VMSSQLITE], system logicals VMSSQLITE$ROOT and SQLITE3$SHR);
$! leaves it as it was.  Works in [.ICHK] of the current directory.
$ set noon
$ set process/parse_style=extended
$ say = "write sys$output"
$ here = f$environment("DEFAULT")
$ tree = here - "]" + "." + p1 + "]"
$ kitdir = tree - "]" + ".KIT_X86_64]"
$ test_src = tree - "]" + ".VMSPORT.KIT]KIT_TEST.C"
$! Process logicals left by a build or test would hide the system ones.
$ if f$trnlnm("VMSSQLITE$ROOT", "LNM$PROCESS_TABLE") .nes. "" then deassign/process VMSSQLITE$ROOT
$ if f$trnlnm("SQLITE3$SHR", "LNM$PROCESS_TABLE") .nes. "" then deassign/process SQLITE3$SHR
$ say "=== INSTALL from ", kitdir
$ product install VMSSQLITE /producer=ISSINOHO /base_system=X86VMS /source='kitdir' /options=noconfirm /log
$ say "=== install status ", $status
$ product show product VMSSQLITE /producer=ISSINOHO
$ say "=== VERIFY"
$ say "startup procedure: [", f$search("SYS$STARTUP:VMSSQLITE$STARTUP.COM"), "]"
$ show logical VMSSQLITE$ROOT
$ show logical SQLITE3$SHR
$ directory/nohead/notrail VMSSQLITE$ROOT:[000000...]*.*
$ if f$search("ICHK.DIR") .eqs. "" then create/directory [.ICHK]
$ set default [.ICHK]
$ if f$search("*.*;*", 1) .nes. "" then delete/nolog *.*;*
$ say "=== CLANG, OBJECT LIBRARY"
$ clang = "$sys$system:clang.exe"
$ clang "-I/vmssqlite$root/include" -c 'test_src' -o kit_test_clang.obj
$ link /executable=kit_test_clang.exe kit_test_clang.obj, VMSSQLITE$ROOT:[LIB]LIBSQLITE3.OLB/library
$ kit_test = "$" + f$parse("kit_test_clang.exe")
$ kit_test clang.db
$ say "=== VSI C, SHAREABLE IMAGE"
$ cc /pointer_size=64 /names=as_is /include=VMSSQLITE$ROOT:[INCLUDE] /object=kit_test_cc.obj 'test_src'
$ link /executable=kit_test_cc.exe kit_test_cc.obj, sys$input/options
SQLITE3$SHR/SHAREABLE
$ kit_test = "$" + f$parse("kit_test_cc.exe")
$ kit_test cc.db
$ say "=== THE SHELL"
$ @VMSSQLITE$ROOT:[COM]VMSSQLITE$SETUP
$ define/user sys$output shell.out
$ sqlite3 cc.db "select count(*) || ' ' || sum(a) from t;"
$ open/read/error=no_out f shell.out
$ read/end=no_line f line
$ close f
$ if line .eqs. "1000 500500" then say "SHELL: PASS ", line
$ if line .nes. "1000 500500" then say "SHELL: FAIL [", line, "]"
$ goto shell_done
$no_line:
$ close f
$no_out:
$ say "SHELL: FAIL no output"
$shell_done:
$ delete/symbol/global sqlite3
$ delete/nolog *.*;*
$ set default 'here'
$ say "=== REMOVE"
$ product remove VMSSQLITE /producer=ISSINOHO /options=noconfirm /log
$ say "=== remove status ", $status
$ say "VMSSQLITE$ROOT after removal: [", f$trnlnm("VMSSQLITE$ROOT"), "]"
$ say "SQLITE3$SHR after removal: [", f$trnlnm("SQLITE3$SHR"), "]"
$ say "files after removal: [", f$search("SYS$COMMON:[VMSSQLITE...]*.*"), "]"
$ say "startup after removal: [", f$search("SYS$STARTUP:VMSSQLITE$STARTUP.COM"), "]"
$ product show product VMSSQLITE /producer=ISSINOHO
