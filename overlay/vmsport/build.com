$! BUILD.COM - build SQLite for OpenVMS from the amalgamation
$!
$! Usage:  @[.VMSPORT]BUILD [target] [compiler]
$!         target: ALL (default) or CLEAN
$!         compiler: CLANG (default; x86-64, LP64) or CC (VSI C, ILP32)
$!
$! Products, in [.OBJ_<arch>_<compiler>]:
$!     LIBSQLITE3.OLB   sqlite3.c with our VFS (vmsport/os_vms.c, D5)
$!     SQLITE3.EXE      the shell
$!     SQLITE3$SHR.EXE  the shareable image (symbol vector: sqlite3_shr.symbols)
$!     SQLITE3_SHR_SHELL.EXE  the shell linked against it, for the tests
$! and the install tree other ports build against (as vms-zlib, vms-pcre2):
$!     [.INSTALL_<arch>[_CLANG].INCLUDE]SQLITE3.H, SQLITE3EXT.H
$!     [.INSTALL_<arch>[_CLANG].LIB]LIBSQLITE3.OLB
$!     [.INSTALL_<arch>[_CLANG].BIN]SQLITE3.EXE
$!     [.INSTALL_<arch>[_CLANG].LIB]SQLITE3$SHR.EXE
$! Compile-time options: docs/DECISIONS.md D4.
$!
$ status = 44
$ on control_y then goto done
$ saved_default = f$environment("DEFAULT")
$ proc = f$environment("PROCEDURE")
$ vmsdir = f$parse(proc,,,"DEVICE") + f$parse(proc,,,"DIRECTORY")
$ set default 'vmsdir'
$ set default [-]
$ set process/parse_style=extended
$ define/process decc$argv_parse_style enable
$ define sys$error sys$output
$ arch = f$edit(f$getsyi("ARCH_NAME"), "UPCASE")
$ target = f$edit(p1, "UPCASE")
$ if target .eqs. "" then target = "ALL"
$ comp = f$edit(p2, "UPCASE")
$ if comp .eqs. "" then comp = "CLANG"
$ if comp .eqs. "CLANG" .and. arch .nes. "X86_64"
$ then
$   write sys$error "BUILD: CLANG needs x86-64"
$   goto done
$ endif
$ obj = "[.OBJ_''arch'_''comp']"
$ inst = "[.INSTALL_''arch'"
$ if comp .eqs. "CLANG" then inst = inst + "_CLANG"
$ write sys$output "BUILD: ''target' for ''arch' with ''comp' in ''f$environment("DEFAULT")'"
$ if target .eqs. "CLEAN"
$ then
$   if f$search("''obj'*.*;*") .nes. "" then delete/nolog 'obj'*.*;*
$   if f$search("''inst'...]*.*;*") .nes. "" then delete/nolog 'inst'...]*.*;*
$   status = 1
$   goto finish
$ endif
$ if f$search("OBJ_''arch'_''comp'.DIR") .eqs. "" then create/directory 'obj'
$!
$ opts = "SQLITE_OS_OTHER=1,SQLITE_THREADSAFE=1,SQLITE_DEFAULT_MEMSTATUS=0," + -
         "SQLITE_OMIT_LOAD_EXTENSION,SQLITE_MAX_MMAP_SIZE=0," + -
         "SQLITE_ENABLE_FTS5,SQLITE_ENABLE_RTREE,SQLITE_ENABLE_MATH_FUNCTIONS," + -
         "HAVE_LOCALTIME_R=1,HAVE_GMTIME_R=1,HAVE_USLEEP=1,_LARGEFILE,_USE_STD_STAT"
$ if comp .eqs. "CLANG"
$ then
$   clang = "$sys$system:clang.exe"
$!  clang reads no text libraries: the starlet headers it lacks come from SYS$LIB_C.TLB
$   if f$search("[.VMSPORT]INCLUDE.DIR") .eqs. "" then create/directory [.VMSPORT.INCLUDE]
$   if f$search("[.VMSPORT.INCLUDE]FATDEF.H") .eqs. "" then -
        library/extract=fatdef/output=[.VMSPORT.INCLUDE]FATDEF.H sys$library:sys$lib_c.tlb
$   dl = "-O2 -I. -Ivmsport/include"
$   i = 0
$d_loop:
$   o = f$element(i, ",", opts)
$   if o .eqs. "," then goto d_done
$   dl = dl + " -D" + o
$   i = i + 1
$   goto d_loop
$d_done:
$ else
$   ccflags = "/PREFIX=ALL/NAMES=(AS_IS,SHORTENED)/FLOAT=IEEE/IEEE=DENORM/OPTIMIZE" + -
              "/INCLUDE=([])/DEFINE=(''opts')"
$ endif
$!
$ srcs = "sqlite3.c,[.vmsport]os_vms.c,shell.c"
$ objs = "sqlite3,os_vms,shell"
$ i = 0
$c_loop:
$ s = f$element(i, ",", srcs)
$ if s .eqs. "," then goto c_done
$ o = f$element(i, ",", objs)
$ write sys$output "BUILD: compiling ''s'"
$ if comp .eqs. "CLANG"
$ then
$   us = s
$   if s .eqs. "[.vmsport]os_vms.c" then us = "vmsport/os_vms.c"
$   extra = ""
$   if o .eqs. "shell" then extra = "-pointer-size=argv64"
$   clang 'dl' 'extra' -c 'us' -o 'obj''o'.obj
$ else
$   cc 'ccflags' /object='obj''o'.obj 's'+sys$library:sys$lib_c.tlb/library
$ endif
$ if .not. $status
$ then
$   write sys$error "BUILD: compile failed: ''s'"
$   status = 44
$   goto done
$ endif
$ i = i + 1
$ goto c_loop
$c_done:
$ if f$search("''obj'LIBSQLITE3.OLB") .nes. "" then delete/nolog 'obj'LIBSQLITE3.OLB;*
$ library/create/object 'obj'LIBSQLITE3.OLB 'obj'sqlite3.obj,'obj'os_vms.obj
$ link/executable='obj'SQLITE3.EXE 'obj'shell.obj,'obj'LIBSQLITE3.OLB/library
$ if .not. $status
$ then
$   write sys$error "BUILD: link failed: SQLITE3.EXE"
$   status = 44
$   goto done
$ endif
$ link/shareable='obj'SQLITE3$SHR.EXE 'obj'sqlite3.obj,'obj'os_vms.obj,[.vmsport]SQLITE3_SHR.OPT/option
$ if .not. $status
$ then
$   write sys$error "BUILD: link failed: SQLITE3$SHR.EXE"
$   status = 44
$   goto done
$ endif
$ shrimg = f$parse("SQLITE3$SHR.EXE",obj)
$ define/user sqlite3$shr 'shrimg'
$ link/executable='obj'SQLITE3_SHR_SHELL.EXE 'obj'shell.obj,sys$input/option
sqlite3$shr/shareable
$ if .not. $status
$ then
$   write sys$error "BUILD: link failed: SQLITE3_SHR_SHELL.EXE"
$   status = 44
$   goto done
$ endif
$!
$ if f$search("''inst']INCLUDE.DIR") .eqs. "" then create/directory 'inst'.INCLUDE]
$ if f$search("''inst']LIB.DIR") .eqs. "" then create/directory 'inst'.LIB]
$ if f$search("''inst']BIN.DIR") .eqs. "" then create/directory 'inst'.BIN]
$ copy/nolog sqlite3.h,sqlite3ext.h 'inst'.INCLUDE]
$ copy/nolog 'obj'LIBSQLITE3.OLB,'obj'SQLITE3$SHR.EXE 'inst'.LIB]
$ copy/nolog 'obj'SQLITE3.EXE 'inst'.BIN]
$ purge/nolog 'inst'...],'obj'
$ write sys$output "BUILD: ''obj'LIBSQLITE3.OLB, ''obj'SQLITE3.EXE, install tree ''inst']"
$! the test programs link the library statically: relink them with it
$ @[.vmsport.test]progs 'comp'
$ status = 1
$finish:
$ write sys$output "BUILD: done"
$done:
$ set default 'saved_default'
$ exit status
