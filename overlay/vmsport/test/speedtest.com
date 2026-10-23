$! SPEEDTEST.COM - SQLite's speedtest1 --size 25 in [.RUN], as docs/speedtest1-vms-*.txt
$! were made (compare with docs/speedtest1-host.txt).  P1: CLANG (default) or CC.
$! Run it alone: it measures time.
$ set noon
$ define sys$error sys$output
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ comp = f$edit(p1,"UPCASE")
$ if comp .eqs. "" then comp = "CLANG"
$ set default 'here'
$ set default [--]
$ img = f$parse("[.OBJ_''f$getsyi("ARCH_NAME")'_''comp']SPEEDTEST1.EXE")
$ set default 'here'
$ if f$search("RUN.DIR") .eqs. "" then create/directory [.RUN]
$ set default [.RUN]
$ if f$search("speed.db*;*") .nes. "" then delete/nolog speed.db*;*
$ st = "$" + img
$ st "--size" "25" speed.db
$ if f$search("speed.db*;*") .nes. "" then delete/nolog speed.db*;*
$ set default 'here'
