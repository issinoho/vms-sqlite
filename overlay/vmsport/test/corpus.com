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
$c_loop:
$ f = f$search("[.CORPUS]*.SQL")
$ if f .eqs. "" then goto c_done
$ n = f$parse(f,,,"NAME")
$ define/user sys$input 'f'
$ define/user sys$output 'out''n'.OUT
$ define/user sys$error 'out''n'.ERR
$ sq 'out'corpus.db
$ if f$search("''out'corpus.db*;*") .nes. "" then delete/nolog 'out'corpus.db*;*
$ write sys$output "CORPUS: ''n'"
$ goto c_loop
$c_done:
$ write sys$output "CORPUS: done"
