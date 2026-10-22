$! CROSSGROUP.COM - D5's open point: do SQLite's locks (C RTL fcntl locks on
$! <db>-lock) exclude between two accounts in different UIC groups, and can
$! the two share one database?  Two sessions, one per account, coordinated by
$! marker files in a shared directory.
$!
$!  @CROSSGROUP SETUP <other-username> [CLANG|CC]
$!      In this account: make XGROUP.DIR in the work directory (three levels
$!      above this one, which other groups can usually traverse: SETUP lists
$!      the protection of each directory above it), copy SQLITE3.EXE, XGLOCK.EXE,
$!      WORK.SQL and this procedure into it, grant <other-username> access
$!      to it and its files by ACL (default ACEs, so that each account can
$!      use and delete what the other makes), and make XGROUP.DB.  Nothing
$!      outside [.XGROUP] is changed: the other account also needs E access
$!      to the directories above it, which SETUP reports.
$!  @<xgroup-directory>CROSSGROUP OTHER    in the other account's session
$!  @CROSSGROUP OURS                       in this account, at the same time
$!      (either may start first: each waits up to 30 min for the other)
$!      A: OTHER holds the database EXCLUSIVE for 20 s: OURS must be refused,
$!         reading and writing, then let in once it is released
$!      B: the same the other way round
$!      C: two writers in each account at once, 200 transactions of 5 rows
$!         each: all 4000 rows there, integrity_check ok
$!  Each prints PASS/FAIL lines; OTHER's also go to OTHER_RESULTS.TXT, and
$!  OURS ends with the summary of both.
$ set noon
$ set process/parse_style=extended
$ define/process decc$argv_parse_style enable
$ define sys$error sys$output
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ orig = f$environment("DEFAULT")
$! the shared directory: XGROUP.DIR in the work directory ([.SQLITE-x.VMSPORT.TEST] is
$! three levels down); the copy of this procedure there runs in it
$ hd = f$parse(here,,,"DIRECTORY") - "[" - "]"
$ n = 0
$x_cnt:
$ if f$element(n, ".", hd) .eqs. "." then goto x_cut
$ n = n + 1
$ goto x_cnt
$x_cut:
$ xr = f$element(0, ".", hd)
$ i = 1
$x_join:
$ if i .ge. n - 3 then goto x_done
$ xr = xr + "." + f$element(i, ".", hd)
$ i = i + 1
$ goto x_join
$x_done:
$ xroot = f$parse(here,,,"DEVICE") + "[" + xr + "]"
$! the build's images: [.<tree>.OBJ_<arch>_<compiler>] in the work directory
$ objbase = f$parse(here,,,"DEVICE") + "[" + xr + "." + f$element(n - 3, ".", hd) + ".OBJ_" + f$getsyi("ARCH_NAME")
$! the copy in XGROUP runs where it is
$ if f$element(n - 1, ".", hd) .eqs. "XGROUP" then xroot = ""
$ role = f$edit(p1,"UPCASE")
$ if role .eqs. "SETUP" then goto setup
$ if role .eqs. "OURS" then goto ours
$ if role .eqs. "OTHER" then goto other
$ write sys$output "usage: @CROSSGROUP SETUP <other-username> [CLANG|CC] | OURS | OTHER"
$ exit
$!
$setup:
$ other = f$edit(p2,"UPCASE")
$ comp = f$edit(p3,"UPCASE")
$ if comp .eqs. "" then comp = "CLANG"
$ me = f$edit(f$getjpi("","USERNAME"),"TRIM")
$ uic = f$identifier(other,"NAME_TO_NUMBER")
$ if other .eqs. "" .or. uic .eq. 0
$ then
$   write sys$output "CROSSGROUP: no such account or identifier: ''other'"
$   exit
$ endif
$ ogrp = uic / 65536
$ mgrp = f$getjpi("","GRP")
$ write sys$output "CROSSGROUP: this account ''me' (group %O''f$fao("!OW",mgrp)'), other ''other' (group %O''f$fao("!OW",ogrp)')"
$ if ogrp .eq. mgrp then write sys$output "CROSSGROUP: WARNING, the same UIC group: this would not test across groups"
$ set default 'xroot'
$ if f$search("XGROUP.DIR") .eqs. "" then create/directory [.XGROUP]
$ xdir = f$parse("[.XGROUP]") - ".;"
$ if f$search("''xdir'*.*;*") .nes. "" then delete/nolog 'xdir'*.*;*
$ ace = "access=read+write+execute+delete"
$ set security/acl=((identifier='other','ace'),(identifier='other',options=default,'ace'),-
    (identifier='me',options=default,'ace')) XGROUP.DIR
$ obj = objbase + "_" + comp + "]"
$ copy/nolog 'obj'SQLITE3.EXE,'obj'XGLOCK.EXE 'xdir'
$ copy/nolog 'here'WORK.SQL,'here'CROSSGROUP.COM 'xdir'
$ set security/acl=(identifier='other',access=read+execute) 'xdir'*.*;*
$ set default 'xdir'
$ sq = "$" + xdir + "SQLITE3.EXE"
$ sq xgroup.db "create table t(w integer, n integer); create table xg(x);"
$ set security/acl=(identifier='other','ace') XGROUP.DB
$ write sys$output "CROSSGROUP: ready in ''xdir' (''comp' images):"
$ directory/acl/nohead/notrail
$! the other account must reach this directory: list the directories above
$! it with their protection (it needs E, by protection or ACL, on each)
$ write sys$output "CROSSGROUP: ''other' needs at least E access to each of these (not changed here):"
$ dev = f$parse(xdir,,,"DEVICE")
$ p = f$parse(xdir,,,"DIRECTORY") - "[" - "]"
$up_loop:
$ i = 0
$up_cnt:
$ if f$element(i + 1, ".", p) .eqs. "." then goto up_split
$ i = i + 1
$ goto up_cnt
$up_split:
$ last = f$element(i, ".", p)
$ parent = "000000"
$ if i .gt. 0 then parent = f$extract(0, f$length(p) - f$length(last) - 1, p)
$ dfile = dev + "[" + parent + "]" + last + ".DIR"
$ write sys$output "  ", dfile, "  ", f$file_attributes(dfile, "PRO")
$ if i .eq. 0 then goto up_done
$ p = parent
$ goto up_loop
$up_done:
$ write sys$output "CROSSGROUP: then, in the ''other' account: @''xdir'CROSSGROUP OTHER"
$ write sys$output "CROSSGROUP: and in this one: @''here'CROSSGROUP OURS (SETUP again before each run)"
$ set default 'orig'
$ exit
$!
$! ---- the two roles -------------------------------------------------------
$ours:
$ me = "OURS"
$ you = "OTHER"
$ goto run
$other:
$ me = "OTHER"
$ you = "OURS"
$run:
$ set default 'here'
$ if xroot .nes. "" then set default 'xroot'
$ if xroot .nes. "" then set default [.XGROUP]
$ xdir = f$environment("DEFAULT")
$ xg :== $'xdir'XGLOCK.EXE
$ sq :== $'xdir'SQLITE3.EXE
$ xg_fails == 0
$ if f$search("''me'_RESULTS.TXT") .nes. "" then delete/nolog 'me'_RESULTS.TXT;*
$ open/write r 'me'_RESULTS.TXT           ! open/append needs the file to exist
$ close r
$ write sys$output "CROSSGROUP ''me': ", f$getjpi("","USERNAME"), " group %O", f$fao("!OW",f$getjpi("","GRP")), ", in ", xdir
$ open/write m 'me'_READY.TXT
$ close m
$ xg wait 'you'_READY.TXT 1800
$ if .not. $status then goto abort
$!
$! A: OTHER holds, OURS tries
$ if me .eqs. "OTHER"
$ then
$   call log "A hold" "xg hold xgroup.db 20 A"
$ else
$   xg wait A_HOLDING.TXT 120
$   call log "A refused while OTHER holds" "xg try xgroup.db BUSY"
$   xg wait A_RELEASED.TXT 120
$   call log "A let in after OTHER released" "xg try xgroup.db OK"
$   open/write m A_DONE.TXT
$   close m
$ endif
$! B: OURS holds, OTHER tries
$ if me .eqs. "OURS"
$ then
$   call log "B hold" "xg hold xgroup.db 20 B"
$   xg wait B_DONE.TXT 120
$ else
$   xg wait A_DONE.TXT 120
$   xg wait B_HOLDING.TXT 120
$   call log "B refused while OURS holds" "xg try xgroup.db BUSY"
$   xg wait B_RELEASED.TXT 120
$   call log "B let in after OURS released" "xg try xgroup.db OK"
$   open/write m B_DONE.TXT
$   close m
$ endif
$! C: two writers each, at once (OURS 1 and 2, OTHER 3 and 4)
$ first = 1
$ if me .eqs. "OTHER" then first = 3
$ w = first
$w_loop:
$ open/write c xw_'w'.com
$ write c "$ set noon"
$ write c "$ set process/parse_style=extended"
$ write c "$ define/process decc$argv_parse_style enable"
$ write c "$ sq = ""$''xdir'SQLITE3.EXE"""
$ write c "$ define/user sys$input ''xdir'work.sql"
$ write c "$ sq xgroup.db ""-cmd"" "".log stderr"" ""-cmd"" "".timeout 60000"" ""-cmd"" "".parameter set @w ''w'"""
$ write c "$ open/write d done_''w'.txt"
$ write c "$ close d"
$ close c
$ spawn/nowait/output=xw_'w'.log @xw_'w'.com
$ w = w + 1
$ if w .le. first + 1 then goto w_loop
$ xg wait done_'first'.txt 900
$ if .not. $status then xg_fails == xg_fails + 1
$ w = first + 1
$ xg wait done_'w'.txt 900
$ if .not. $status then xg_fails == xg_fails + 1
$ type xw_'first'.log
$ type xw_'w'.log
$ open/write m 'me'_DONE.TXT
$ close m
$ if me .eqs. "OTHER" then goto finish
$ xg wait OTHER_DONE.TXT 900
$ if .not. $status then xg_fails == xg_fails + 1
$ define/user sys$output c_check.txt
$ sq xgroup.db "select 'rows', count(*), count(distinct w) from t; pragma integrity_check;"
$ type c_check.txt
$ c = "PASS"
$ search/nooutput c_check.txt "rows|4000|4"
$ if $severity .ne. 1 then c = "FAIL"
$ search/nooutput/exact c_check.txt "ok"
$ if $severity .ne. 1 then c = "FAIL"
$ if c .eqs. "FAIL" then xg_fails == xg_fails + 1
$ write sys$output "C four writers, two per account, 4000 rows and integrity_check ok: ", c
$finish:
$ write sys$output "CROSSGROUP ''me': ", xg_fails, " failures"
$ open/append r 'me'_RESULTS.TXT
$ write r "CROSSGROUP ''me': ", xg_fails, " failures"
$ close r
$ if me .eqs. "OURS" .and. f$search("OTHER_RESULTS.TXT") .nes. ""
$ then
$   write sys$output "CROSSGROUP: the other side's results:"
$   type OTHER_RESULTS.TXT
$ endif
$ set default 'orig'
$ exit
$abort:
$ write sys$output "CROSSGROUP ''me': the other side did not start"
$ set default 'orig'
$ exit
$!
$! log <what> <command>: run an xglock step, keep its line, count a failure
$log: subroutine
$ define/user sys$output 'me'_STEP.TXT     ! per side: the two share this directory
$ 'p2'
$ st = $status
$ open/append r 'me'_RESULTS.TXT
$ open/read s 'me'_STEP.TXT
$l_read:
$ read/end=l_end s line
$ write sys$output p1, ": ", line
$ write r p1, ": ", line
$ goto l_read
$l_end:
$ close s
$ close r
$ if .not. st then xg_fails == xg_fails + 1
$ exit
$ endsubroutine
