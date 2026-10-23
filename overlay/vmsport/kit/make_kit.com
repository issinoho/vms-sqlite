$! MAKE_KIT.COM - build the PCSI kit VMSSQLITE for x86-64 (D3, D11)
$!
$! Usage:  @[.VMSPORT.KIT]MAKE_KIT
$! Needs the clang build (@[.VMSPORT]BUILD ALL CLANG): its install tree
$! [.INSTALL_X86_64_CLANG].  Writes the kit to [.KIT_X86_64].  The product
$! description, text module and documentation were put in [.VMSPORT.KIT] by
$! tools/prepare.sh.
$!
$ set noon
$ set process/parse_style=extended
$ status = 44
$ saved_default = f$environment("DEFAULT")
$ proc = f$environment("PROCEDURE")
$ kitdir = f$parse(proc,,,"DEVICE") + f$parse(proc,,,"DIRECTORY")
$ set default 'kitdir'
$ set default [--]
$ arch = f$edit(f$getsyi("ARCH_NAME"), "UPCASE")
$ base = "X86VMS"
$ if arch .nes. "X86_64"
$ then
$   write sys$error "MAKE_KIT: x86-64 only (D2), not ''arch'"
$   goto done
$ endif
$!
$! KIT_PRODUCER, KIT_PRODUCT, PCSI_VERSION, KIT_VERSION from kit.env
$ open/read/error=no_env env [.VMSPORT.KIT]KIT.ENV
$env_loop:
$ read/end=env_done env line
$ name = f$element(0, "=", line)
$ 'name' = f$element(1, "=", line)
$ goto env_loop
$no_env:
$ write sys$error "MAKE_KIT: no [.VMSPORT.KIT]KIT.ENV; run tools/prepare.sh"
$ goto done
$env_done:
$ close env
$!
$ inst = "[.INSTALL_X86_64_CLANG"
$ need = "''inst'.BIN]SQLITE3.EXE,''inst'.LIB]SQLITE3$SHR.EXE,''inst'.LIB]LIBSQLITE3.OLB," -
       + "''inst'.INCLUDE]SQLITE3.H,''inst'.INCLUDE]SQLITE3EXT.H"
$ i = 0
$need_loop:
$ f = f$element(i, ",", need)
$ if f .eqs. "," then goto need_done
$ if f$search(f, 1) .eqs. ""
$ then
$   write sys$error "MAKE_KIT: no ''f'; build first (BUILD ALL CLANG)"
$   goto done
$ endif
$ i = i + 1
$ goto need_loop
$need_done:
$!
$! Gather the files flat in [.KIT_<arch>.MAT]: PRODUCT PACKAGE looks each one
$! up by name in the material directory (destinations are in the PDF).
$ mat = "[.KIT_''arch'.MAT]"
$ out = "[.KIT_''arch']"
$ if f$search("KIT_''arch'.DIR") .eqs. "" then create/directory 'out'
$ if f$search("[.KIT_''arch']MAT.DIR") .eqs. "" then create/directory 'mat'
$ if f$search("''out'*.PCSI;*", 2) .nes. "" then delete/nolog 'out'*.PCSI;*
$ if f$search("''mat'*.*;*", 3) .nes. "" then delete/nolog 'mat'*.*;*
$ copy/nolog 'inst'.BIN]SQLITE3.EXE 'mat'
$ copy/nolog 'inst'.LIB]SQLITE3$SHR.EXE,LIBSQLITE3.OLB 'mat'
$ copy/nolog 'inst'.INCLUDE]SQLITE3.H,SQLITE3EXT.H 'mat'
$ copy/nolog [.VMSPORT.KIT]VMSSQLITE$*.COM 'mat'
$ copy/nolog [.VMSPORT.KIT]README.VMS,COPYING.,NEWS.,SQLITE3.TXT 'mat'
$ matspec = f$parse(mat,,,"DEVICE","NO_CONCEAL") + f$parse(mat,,,"DIRECTORY","NO_CONCEAL")
$!
$ write sys$output "MAKE_KIT: ''KIT_PRODUCER' ''base' ''KIT_PRODUCT' ''PCSI_VERSION' (SQLite ''KIT_VERSION')"
$ product package 'KIT_PRODUCT' -
    /producer='KIT_PRODUCER' /base_system='base' /version='PCSI_VERSION' -
    /source=[.VMSPORT.KIT]PRODUCT-'base'.PCSI$DESC -
    /material='matspec' -
    /destination='out' -
    /format=sequential -
    /options=noconfirm /log
$ status = $status
$ kit = f$search("''out'*.PCSI", 4)
$ if kit .nes. "" then write sys$output "MAKE_KIT: kit ", kit
$ if kit .eqs. "" then status = 44
$done:
$ set default 'saved_default'
$ exit status
