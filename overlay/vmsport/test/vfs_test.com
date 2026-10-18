$! VFS_TEST.COM - build and run VFS_TEST.C against the library.
$! P1: CLANG (default) or CC.
$ set noon
$ set process/parse_style=extended
$ define/process decc$argv_parse_style enable
$ define sys$error sys$output
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ comp = f$edit(p1,"UPCASE")
$ if comp .eqs. "" then comp = "CLANG"
$ set default 'here'
$ set default [--]
$ obj = f$parse("[.OBJ_''f$getsyi("ARCH_NAME")'_''comp']") - ".;"
$ set default 'here'
$ if comp .eqs. "CLANG"
$ then
$   clang = "$sys$system:clang.exe"
$   clang -O1 -I../.. -D_LARGEFILE -D_USE_STD_STAT -c vfs_test.c -o 'obj'vfs_test.obj
$ else
$   cc/prefix=all/names=(as_is,shortened)/float=ieee/include=([--])/object='obj'vfs_test.obj vfs_test.c
$ endif
$ link/executable='obj'vfs_test.exe 'obj'vfs_test.obj,'obj'libsqlite3.olb/library
$ if f$search("RUN.DIR") .eqs. "" then create/directory [.RUN]
$ set default [.RUN]
$ run 'obj'vfs_test.exe
$ set default 'here'
