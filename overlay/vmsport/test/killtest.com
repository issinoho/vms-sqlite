$! KILLTEST.COM - durability: a writer (KILLWORK.SQL: transactions of 100
$! rows plus a counter) is stopped with STOP/ID at random moments, P2 times
$! (default 20); after each kill the database must be intact and hold
$! exactly 100 rows per counted transaction.  P1: CLANG or CC.
$! P3: the synchronous setting (default FULL).
$ set noon
$ set process/parse_style=extended
$ define/process decc$argv_parse_style enable
$ define sys$error sys$output
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ comp = f$edit(p1,"UPCASE")
$ if comp .eqs. "" then comp = "CLANG"
$ cycles = 20
$ if p2 .nes. "" then cycles = f$integer(p2)
$ sync = "FULL"
$ if p3 .nes. "" then sync = f$edit(p3,"UPCASE")
$ set default 'here'
$ set default [--]
$ obj = f$parse("[.OBJ_''f$getsyi("ARCH_NAME")'_''comp']") - ".;"
$ set default 'here'
$ sq :== $'obj'sqlite3.exe
$ waitdone :== $'obj'waitdone.exe
$ if f$search("RUN.DIR") .eqs. "" then create/directory [.RUN]
$ set default [.RUN]
$ if f$search("kill.db*;*") .nes. "" then delete/nolog kill.db*;*
$ sq kill.db "create table t(id integer primary key, k integer, v blob); create table c(n integer); insert into c values(0);"
$ open/write w killw.com
$ write w "$ set noon"
$ write w "$ define/user sys$input ''here'killwork.sql"
$ write w "$ sq kill.db ""-cmd"" "".timeout 60000"" ""-cmd"" ""pragma synchronous=''sync'"""
$ close w
$ broken = 0
$ i = 1
$k_loop:
$ if i .gt. cycles then goto k_done
$ spawn/nowait/process=KILLW_'f$getjpi("","PID")'/output=killw.log @killw.com
$ pname = "KILLW_" + f$getjpi("","PID")
$! run for 1 to 5 seconds (the tick is the time of day: random enough)
$ wait_s = 1 + f$integer(f$extract(21, 2, f$time())) / 25
$ waitdone 1 'wait_s'
$ ctx = ""
$ pid = f$pid(ctx)
$f_loop:
$ if pid .eqs. "" then goto f_done
$ if f$getjpi(pid,"PRCNAM") .eqs. pname then goto f_done
$ pid = f$pid(ctx)
$ goto f_loop
$f_done:
$ if pid .nes. "" then stop/id='pid'
$! let the process go, then check: a hot journal is rolled back by this open
$ k = 0
$g_loop:
$ if f$getjpi("","PRCCNT") .eq. 0 .or. k .ge. 30 then goto g_done
$ waitdone 1 1
$ k = k + 1
$ goto g_loop
$g_done:
$ hot = f$search("kill.db-journal") .nes. ""
$ define/user sys$output killcheck.txt
$ sq kill.db "select case when count(*) = 100 * (select n from c) then 'CONSISTENT' else 'BROKEN' end, count(*), (select n from c) from t; pragma integrity_check;"
$ open/read r killcheck.txt
$ read r l1
$ read r l2
$ close r
$ write sys$output "KILL ''i': after ''wait_s' s, journal left ''hot': ''l1' ''l2'"
$ if f$extract(0, 10, l1) .nes. "CONSISTENT" .or. l2 .nes. "ok" then broken = broken + 1
$ i = i + 1
$ goto k_loop
$k_done:
$ write sys$output "KILL: ''cycles' kills (synchronous=''sync'), ''broken' broken"
$ set default 'here'
