$! VMS_PROBE.COM - compile and run the Phase 0 probes in this directory.
$! P1: CLANG (default) or CC (VSI C).   P2: FEATURES to define the C RTL
$! feature logicals SQLite would set in its images (DECC$FILE_SHARING etc.),
$! or empty for the C RTL defaults.  P3: the probes to run (default all).
$ set noon
$ set process/parse_style=extended
$ define/process decc$argv_parse_style enable
$ define sys$error sys$output
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ set default 'here'
$ comp = f$edit(p1,"UPCASE")
$ if comp .eqs. "" then comp = "CLANG"
$ write sys$output "PROBE compiler ''comp', features ''p2', ", f$getsyi("ARCH_NAME"), " ", f$getsyi("VERSION")
$ write sys$output "PROBE ODS-5: ", f$getdvi(here,"ODS5")
$ if comp .eqs. "CLANG"
$ then
$   clang :== $sys$system:clang.exe
$   clang --version
$!  clang does not read text libraries: take the starlet headers it lacks from SYS$LIB_C.TLB
$   if f$search("fatdef.h") .eqs. "" then -
        library/extract=fatdef/output=fatdef.h sys$library:sys$lib_c.tlb
$ else
$   cc/version
$ endif
$ probes = "p_misc,p_leak,p_lkns,p_mp,p_qio"
$ if p3 .nes. "" then probes = f$edit(p3,"LOWERCASE")
$ i = 0
$c_loop:
$ p = f$element(i, ",", probes)
$ if p .eqs. "," then goto c_done
$ if f$search("''p'.exe") .nes. "" then delete/nolog 'p'.exe;*
$ if comp .eqs. "CLANG"
$ then
$   clang -O1 -I. -D_LARGEFILE -D_USE_STD_STAT -c 'p'.c -o 'p'.obj
$ else
$   cc/prefix=all/define=(_LARGEFILE,_USE_STD_STAT)/object='p'.obj 'p'.c+sys$library:sys$lib_c.tlb/library
$ endif
$ if $status then link/executable='p'.exe 'p'.obj
$ i = i + 1
$ goto c_loop
$c_done:
$ if f$edit(p2,"UPCASE") .eqs. "FEATURES"
$ then
$!  after compiling: they change how clang itself names its output files
$   define/process DECC$FILE_SHARING ENABLE
$   define/process DECC$ALLOW_REMOVE_OPEN_FILES ENABLE
$   define/process DECC$EFS_CHARSET ENABLE
$   define/process DECC$EFS_CASE_PRESERVE ENABLE
$   define/process DECC$FILENAME_UNIX_REPORT ENABLE
$   define/process DECC$FILENAME_UNIX_NO_VERSION ENABLE
$   define/process DECC$READDIR_DROPDOTNOTYPE ENABLE
$ endif
$ if f$search("*.step;*") .nes. "" then delete/nolog *.step;*
$ if f$search("mp_test.db*;*") .nes. "" then delete/nolog mp_test.db*;*
$ mp :== $'here'p_mp.exe
$ qio :== $'here'p_qio.exe
$ i = 0
$r_loop:
$ p = f$element(i, ",", probes)
$ if p .eqs. "," then goto r_done
$ write sys$output "PROBE --- ''p'"
$ if p .eqs. "p_misc" then run p_misc
$ if p .eqs. "p_leak" then run p_leak
$ if p .eqs. "p_lkns" then run p_lkns
$ if p .eqs. "p_mp" then gosub two
$ if p .eqs. "p_qio" then gosub two
$ i = i + 1
$ goto r_loop
$!
$! two processes: A in a subprocess, B here; B waits for A before it exits
$two:
$ if f$search("*.step;*") .nes. "" then delete/nolog *.step;*
$ if f$search("*_test.db*;*") .nes. "" then delete/nolog *_test.db*;*
$ cmd = p - "p_"
$ if cmd .eqs. "qio" then cmd = "qio"
$ spawn/nowait/output='here''p'_a.log 'cmd' a
$ 'cmd' b
$ if f$search("qio_test.db") .nes. "" then -
    pipe directory/full qio_test.db | search sys$pipe "End of file","Record format"
$ type 'p'_a.log
$ if f$search("''p'_a.log;-1") .nes. "" then purge/nolog 'p'_a.log
$ return
$r_done:
$ if f$search("*.step;*") .nes. "" then delete/nolog *.step;*
$ if f$search("*_test.db*;*") .nes. "" then delete/nolog *_test.db*;*
$ write sys$output "PROBE end"
$ if f$trnlnm("DECC$FILE_SHARING","LNM$PROCESS") .nes. "" then -
    deassign/process DECC$FILE_SHARING
$ exit
