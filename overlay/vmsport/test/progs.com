$! PROGS.COM - build SQLite's test programs against the library:
$! MPTEST.EXE (multi-process), THREADTEST4.EXE (threads), SPEEDTEST1.EXE.
$! P1: CLANG (default) or CC.  Images go to [.OBJ_<arch>_<compiler>].
$ set noon
$ set process/parse_style=extended
$ define/process decc$argv_parse_style enable
$ define sys$error sys$output
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ comp = f$edit(p1,"UPCASE")
$ if comp .eqs. "" then comp = "CLANG"
$ set default 'here'
$ set default [--]
$ obj = f$parse("[.OBJ_''f$getsyi("ARCH_NAME")'_''comp']") - ".;"
$ progs = "mptest/mptest.c,test/threadtest4.c,test/speedtest1.c"
$ names = "mptest,threadtest4,speedtest1"
$ i = 0
$p_loop:
$ s = f$element(i, ",", progs)
$ if s .eqs. "," then goto p_done
$ n = f$element(i, ",", names)
$ if comp .eqs. "CLANG"
$ then
$   clang = "$sys$system:clang.exe"
$   clang -O2 -I. -D_LARGEFILE -D_USE_STD_STAT -pointer-size=argv64 -c 's' -o 'obj''n'.obj
$ else
$   vs = "[." + f$element(0, "/", s) + "]" + f$element(1, "/", s)
$   cc/prefix=all/names=(as_is,shortened)/float=ieee/ieee=denorm/include=([])/define=(_LARGEFILE,_USE_STD_STAT) -
        /object='obj''n'.obj 'vs'
$ endif
$ extra = ""
$ if n .nes. "mptest" then goto p_link
$ if comp .eqs. "CLANG"
$ then
$   clang -O2 -I. -c vmsport/test/noext.c -o 'obj'noext.obj
$ else
$   cc/prefix=all/names=(as_is,shortened)/include=([])/object='obj'noext.obj [.vmsport.test]noext.c
$ endif
$ extra = ",''obj'noext.obj"
$p_link:
$ link/executable='obj''n'.exe 'obj''n'.obj'extra','obj'libsqlite3.olb/library
$ write sys$output "PROGS: ''n' ", f$search("''obj'''n'.exe") .nes. ""
$ i = i + 1
$ goto p_loop
$p_done:
$ set default 'here'
