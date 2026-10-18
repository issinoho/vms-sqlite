$! CONCURRENT.COM - P2 processes (default 4) write one database at once:
$! each runs WORK.SQL (200 transactions of 5 rows, BEGIN IMMEDIATE, with a
$! busy timeout) while this process reads (READER.SQL).  Then every row must
$! be there and the database intact.
$! P1: CLANG (default) or CC.
$ set noon
$ set process/parse_style=extended
$ define/process decc$argv_parse_style enable
$ define sys$error sys$output
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ comp = f$edit(p1,"UPCASE")
$ if comp .eqs. "" then comp = "CLANG"
$ n = 4
$ if p2 .nes. "" then n = f$integer(p2)
$ set default 'here'
$ set default [--]
$ obj = f$parse("[.OBJ_''f$getsyi("ARCH_NAME")'_''comp']") - ".;"
$ set default 'here'
$ if f$search("''obj'waitdone.exe") .eqs. ""
$ then
$   if comp .eqs. "CLANG"
$   then
$     clang = "$sys$system:clang.exe"
$     clang -c waitdone.c -o 'obj'waitdone.obj
$   else
$     cc/object='obj'waitdone.obj waitdone.c
$   endif
$   link/executable='obj'waitdone.exe 'obj'waitdone.obj
$ endif
$ sq :== $'obj'sqlite3.exe
$ waitdone :== $'obj'waitdone.exe
$ if f$search("RUN.DIR") .eqs. "" then create/directory [.RUN]
$ set default [.RUN]
$ if f$search("*.*;*") .nes. "" then delete/nolog *.*;*
$ sq conc.db "create table t(w integer, n integer);"
$ i = 1
$s_loop:
$ if i .gt. n then goto s_done
$ open/write c w_'i'.com
$ write c "$ set noon"
$ write c "$ define/user sys$input ''here'work.sql"
$ write c "$ sq conc.db ""-cmd"" "".timeout 60000"" ""-cmd"" "".parameter set @w ''i'"""
$ write c "$ open/write d done_''i'.txt"
$ write c "$ close d"
$ close c
$ spawn/nowait/output=w_'i'.log @w_'i'.com
$ i = i + 1
$ goto s_loop
$s_done:
$! a reader: every snapshot must hold whole transactions (5 rows each)
$ define/user sys$input 'here'reader.sql
$ sq conc.db
$ waitdone 'n' 900
$ i = 1
$l_loop:
$ if i .gt. n then goto l_done
$ type w_'i'.log
$ i = i + 1
$ goto l_loop
$l_done:
$ sq conc.db "select 'TOTAL', count(*), count(distinct w), sum(n) from t; pragma integrity_check;"
$ directory/size/nohead/notrail
$ set default 'here'
