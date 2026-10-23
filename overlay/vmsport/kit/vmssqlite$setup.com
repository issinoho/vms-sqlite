$! VMSSQLITE$SETUP.COM - DCL symbols for SQLite for OpenVMS (from LOGIN.COM or SYLOGIN.COM)
$!
$!   $ @VMSSQLITE$ROOT:[COM]VMSSQLITE$SETUP
$!   $ sqlite3 test.db "select sqlite_version();"
$!
$! DCL upper-cases arguments that are not quoted: quote the shell's options
$! ("-header", "-csv") and SQL.
$ sqlite3 :== $VMSSQLITE$ROOT:[BIN]SQLITE3.EXE
$ exit 1
