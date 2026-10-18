$! RECON.COM - Phase 0: does SQLite 3.54.1 compile and run as it is?
$! P1: CLANG or CC.  Builds [.RECON_<P1>]SQLITE3.EXE from sqlite3.c and
$! shell.c with SQLite's own unix VFS, then runs RECON_SQL.TXT through it.
$ set noon
$ set process/parse_style=extended
$ define/process decc$argv_parse_style enable
$ define sys$error sys$output
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ set default 'here'
$ comp = f$edit(p1,"UPCASE")
$ out = "[.recon_''comp']"
$ if f$search("recon_''comp'.dir") .eqs. "" then create/directory 'out'
$ opts = "SQLITE_THREADSAFE=1,SQLITE_OMIT_LOAD_EXTENSION,SQLITE_MAX_MMAP_SIZE=0,SQLITE_ENABLE_FTS5,SQLITE_ENABLE_RTREE,SQLITE_ENABLE_MATH_FUNCTIONS,_LARGEFILE,_USE_STD_STAT"
$ t0 = f$time()
$ if comp .eqs. "CLANG"
$ then
$   clang :== $sys$system:clang.exe
$   dl = ""
$   i = 0
$cl:
$   o = f$element(i, ",", opts)
$   if o .eqs. "," then goto cld
$   dl = dl + " -D" + o
$   i = i + 1
$   goto cl
$cld:
$   clang -O2 'dl' -c sqlite3.c -o 'out'sqlite3.obj
$   write sys$output "RECON sqlite3.c clang status ", $status
$   clang -O2 -pointer-size=argv64 'dl' -c shell.c -o 'out'shell.obj
$   write sys$output "RECON shell.c clang status ", $status
$ else
$   cc/prefix=all/float=ieee/ieee=denorm/optimize/define=('opts')/object='out'sqlite3.obj sqlite3.c
$   write sys$output "RECON sqlite3.c cc status ", $status
$   cc/prefix=all/float=ieee/ieee=denorm/optimize/define=('opts')/object='out'shell.obj shell.c
$   write sys$output "RECON shell.c cc status ", $status
$ endif
$ write sys$output "RECON compile ", t0, " - ", f$time()
$ link/executable='out'sqlite3.exe 'out'shell.obj,'out'sqlite3.obj
$ write sys$output "RECON link status ", $status
$ if f$search("''out'sqlite3.exe") .eqs. "" then exit
$ img = f$parse("''out'sqlite3.exe")
$ sq :== $'img'
$ define/user sys$input recon_sql.txt
$ sq
$ write sys$output "RECON end"
