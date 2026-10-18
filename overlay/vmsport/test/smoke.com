$! SMOKE.COM - Phase 1 smoke test: [.TEST.RUN]SMOKE.DB through the shell.
$! P1: CLANG (default) or CC.
$ set noon
$ define sys$error sys$output
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ comp = f$edit(p1,"UPCASE")
$ if comp .eqs. "" then comp = "CLANG"
$ set default 'here'
$ set default [--]
$ img = f$parse("[.OBJ_''f$getsyi("ARCH_NAME")'_''comp']SQLITE3.EXE")
$ sq = "$" + img
$ set default 'here'
$ if f$search("RUN.DIR") .eqs. "" then create/directory [.RUN]
$ set default [.RUN]
$ if f$search("*.*;*") .nes. "" then delete/nolog *.*;*
$ write sys$output "SMOKE: ", img
$ sq -version
$ define/user sys$input [-]smoke.sql
$ sq smoke.db
$ write sys$output "SMOKE: files"
$ directory/size=all/nohead/notrail
$ pipe directory/full smoke.db | search sys$pipe "Record format","End of file","Allocation"
$ write sys$output "SMOKE: again, a Unix-style name, read only"
$ sq "-readonly" "../RUN/smoke.db" "select count(*) from t; pragma integrity_check;"
$ write sys$output "SMOKE: the shell linked against SQLITE3$SHR"
$ shr = "$" + f$parse("SQLITE3_SHR_SHELL.EXE;",img)
$ shrimg = f$parse("SQLITE3$SHR.EXE;",img)
$ define/user sqlite3$shr 'shrimg'
$ shr smoke.db "select sqlite_version(), count(*) from t; pragma integrity_check;"
$ write sys$output "SMOKE: end"
