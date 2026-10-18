$! MPTEST.COM - run SQLite's mptest scripts (P2: list, default all four)
$! in [.RUN]; mptest starts its own client processes.  P1: CLANG or CC.
$ set noon
$ set process/parse_style=extended
$ define/process decc$argv_parse_style enable
$ define sys$error sys$output
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ comp = f$edit(p1,"UPCASE")
$ if comp .eqs. "" then comp = "CLANG"
$ scripts = p2
$ if scripts .eqs. "" then scripts = "config01,config02,crash01,multiwrite01"
$ set default 'here'
$ set default [--]
$ obj = f$parse("[.OBJ_''f$getsyi("ARCH_NAME")'_''comp']") - ".;"
$ mptest :== $'obj'mptest.exe
$ waitdone :== $'obj'waitdone.exe
$ set default 'here'
$ if f$search("RUN.DIR") .eqs. "" then create/directory [.RUN]
$ set default [.RUN]
$ i = 0
$m_loop:
$ t = f$element(i, ",", scripts)
$ if t .eqs. "," then goto m_done
$ if f$search("*.*;*") .nes. "" then delete/nolog *.*;*
$ write sys$output "MPTEST: ''t' ", f$time()
$! a Unix-style path: mptest finds --source files by splitting the script's
$! path at "/"
$ mptest mp.db "../../../mptest/''t'.test" "--timeout" "60000" 'p3'
$ write sys$output "MPTEST: ''t' status ", $status, " ", f$time()
$! mptest's clients are subprocesses of this process: let them end first
$ k = 0
$k_loop:
$ if f$getjpi("","PRCCNT") .eq. 0 .or. k .ge. 90 then goto k_done
$ waitdone 1 2
$ k = k + 1
$ goto k_loop
$k_done:
$ if f$getjpi("","PRCCNT") .gt. 0 then write sys$output "MPTEST: ", f$getjpi("","PRCCNT"), " client processes still running"
$ i = i + 1
$ goto m_loop
$m_done:
$ set default 'here'
