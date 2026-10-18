$! CORPUS.COM - run every [.CORPUS]*.SQL through the shell, each on a new
$! file database, into [.RUN.CORPUS_<compiler>]<name>.OUT (stdout) and .ERR
$! (stderr); tools/corpus.sh compares them with the host's.  P1: CLANG or CC.
$ set noon
$ set process/parse_style=extended
$ define/process decc$argv_parse_style enable
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ comp = f$edit(p1,"UPCASE")
$ if comp .eqs. "" then comp = "CLANG"
$ set default 'here'
$ set default [--]
$ sq = "$" + f$parse("[.OBJ_''f$getsyi("ARCH_NAME")'_''comp']SQLITE3.EXE")
$ set default 'here'
$ if f$search("RUN.DIR") .eqs. "" then create/directory [.RUN]
$ out = "[.RUN.CORPUS_''comp']"
$ if f$search("[.RUN]CORPUS_''comp'.DIR") .eqs. "" then create/directory 'out'
$ if f$search("''out'*.*;*") .nes. "" then delete/nolog 'out'*.*;*
$ nrun = 0
$c_loop:
$ nrun = nrun + 1
$ if nrun .gt. 100 then goto c_done
$! stream 1: the search for the next script, kept apart from stream 2 below
$! (a shared context restarts this loop at the first script for ever)
$ f = f$search("[.CORPUS]*.SQL", 1)
$ if f .eqs. "" then goto c_done
$ n = f$parse(f,,,"NAME")
$ define/user sys$input 'f'
$ define/user sys$output 'out''n'.OUT
$ define/user sys$error 'out''n'.ERR
$ sq 'out'corpus.db
$! every time (an f$search check here keeps its context between scripts and
$! misses every other database)
$ define/user sys$output nl:
$ define/user sys$error nl:
$ delete/nolog 'out'corpus.db*;*
$ write sys$output "CORPUS: ''n'"
$ goto c_loop
$c_done:
$ write sys$output "CORPUS: done"
