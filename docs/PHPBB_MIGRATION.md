# Moving a phpBB board from MariaDB to SQLite

How to move a phpBB 3.3 board served by lighttpd for OpenVMS (vms-lighttpd) and PHP for OpenVMS
(8.5.12-vms2 or later) from its MariaDB database to an SQLite database. Then point the board's
`config.php` at the new database.

phpBB has no converter between database systems. So phpBB itself creates the SQLite database
(a fresh install of the same release, with the same extensions), and
`contrib/phpbb_mysql_to_sqlite.php` copies every table's rows into it from MariaDB. The
board's files (attachments, avatars, styles, extensions) stay where they are. MariaDB is only
read, so going back means restoring the old `config.php`.

PHP's own configuration (the pool's `PHP.INI`) needs no change: `sqlite3` and `pdo_sqlite` are
built into 8.5.12-vms2, and the database lives outside the document root.

## Before you start

**Names used below:** the commands use these examples; replace them with your own.

| | example |
|---|---|
| the phpBB directory (holds `config.php`) | `DISK$WEB:[LIGHTTPD.HTDOCS.PHPBB]`, Unix form `/disk$web/lighttpd/htdocs/phpbb` |
| a new directory for the database, **outside** the document root | `DISK$WEB:[LIGHTTPD.DB]`, `/disk$web/lighttpd/db` |
| a scratch directory for the fresh install | `DISK$WEB:[LIGHTTPD.SCRATCH]`, `/disk$web/lighttpd/scratch` |
| the service account | `LIGHTTPD` (vms-lighttpd D12) |

Run everything as SYSTEM, with the extended parse style (phpBB's command-line tools take
case-sensitive arguments) and the PHP and MariaDB symbols:

```
$ SET PROCESS/PARSE_STYLE=EXTENDED
$ @VMSPHP$ROOT:[COM]VMSPHP$SETUP
$ @VMSMARIADB$ROOT:[000000]VMSMARIADB$SETUP
$ DEFINE/PROCESS COLUMNS "80"       ! phpbbcli (Symfony Console) needs these
$ DEFINE/PROCESS LINES "24"
```

**Check three things:**

1. **PHP:** `php -m` lists `pdo_mysql`, `pdo_sqlite` and `sqlite3` (PHP for OpenVMS 8.5.12-vms2).
2. **The phpBB release** of the board. Its database name and table prefix are in `config.php`
   (`SEARCH DISK$WEB:[LIGHTTPD.HTDOCS.PHPBB]config.php "$dbname","$table_prefix"`); then

   ```
   $ mariadb "-h" 127.0.0.1 "-u" root "-p" "-e" "select config_value from phpbb_config where config_name='version'" <dbname>
   ```

   You need that exact phpBB release's zip (e.g. 3.3.19) for step 2.
3. **The extensions:** every extension the board knows (enabled or disabled) must also be
   enabled in the new database, because an extension's tables and columns are made by
   enabling it:

   ```
   $ mariadb "-h" 127.0.0.1 "-u" root "-p" "-e" "select ext_name, ext_active from phpbb_ext" <dbname>
   ```

**Downtime:** the board is stopped from step 4 to step 7. Steps 1-3 can be done the day before.
On a test board of 3,575 rows the copy took 2 seconds; a board with years of posts takes
longer (the copy prints each table as it goes).

## 1. Back up

```
$ CREATE/DIRECTORY DISK$WEB:[LIGHTTPD.DB]
$ COPY DISK$WEB:[LIGHTTPD.HTDOCS.PHPBB]config.php DISK$WEB:[LIGHTTPD.DB]CONFIG.PHP_MYSQL
$ mariadb_dump "-h" 127.0.0.1 "-u" root "-p" "--single-transaction" "--result-file=/disk$web/lighttpd/db/board-mysql.sql" <dbname>
```

`CONFIG.PHP_MYSQL` is what you restore to go back (step 9). The MariaDB database itself is not
changed at any point.

## 2. Make the empty SQLite database: a fresh install of the same release

Put phpBB's release files (the same version as the board) into `DISK$WEB:[LIGHTTPD.SCRATCH]`, as you did for the
board (e.g. unzip on a PC, then `sftp put -r`). Then give the scratch copy the board's extensions:

```
$ BACKUP DISK$WEB:[LIGHTTPD.HTDOCS.PHPBB.EXT...]*.*;* DISK$WEB:[LIGHTTPD.SCRATCH.EXT...]*.*;*/NEW_VERSION
```

Write an installer configuration, `DISK$WEB:[LIGHTTPD.SCRATCH]install.yml`. The admin account, board name and
e-mail settings here are thrown away: the copy replaces them with the board's own.

```yaml
installer:
    admin:
        name: admin
        password: Scratch-Only-123
        email: admin@example.org
    board:
        lang: en
        name: scratch
        description: scratch
    database:
        dbms: sqlite3
        dbhost: /disk$web/lighttpd/db/phpbb.sqlite3
        dbport: ~
        dbuser: ~
        dbpasswd: ~
        dbname: ~
        table_prefix: phpbb_
    email:
        enabled: false
        smtp_delivery : ~
        smtp_host: ~
        smtp_port: ~
        smtp_auth: ~
        smtp_user: ~
        smtp_pass: ~
    server:
        cookie_secure: false
        server_protocol: http://
        force_server_vars: false
        server_name: localhost
        server_port: 80
        script_path: /
    extensions: []
```

`dbhost` is the new database file, in Unix form; `table_prefix` must be the board's
`$table_prefix`. Install, then enable every extension from "Before you start" (3), for example:

```
$ SET DEFAULT DISK$WEB:[LIGHTTPD.SCRATCH]
$ php install/phpbbcli.php install /disk$web/lighttpd/scratch/install.yml
$ php bin/phpbbcli.php extension:enable phpbb/viglink
$ php bin/phpbbcli.php extension:enable <vendor>/<name>
```

The installer enables `phpbb/viglink` itself; its line does no harm. The scratch tree is not used
again after step 5.

## 3. Check the copy before the real one (optional; the board keeps running)

The copy only reads MariaDB, so it can be run once now as a rehearsal, with the board still up:

```
$ COPY DISK$WEB:[LIGHTTPD.DB]PHPBB.SQLITE3 DISK$WEB:[LIGHTTPD.DB]REHEARSAL.SQLITE3
$ php /disk$web/lighttpd/scratch/phpbb_mysql_to_sqlite.php /disk$web/lighttpd/htdocs/phpbb/config.php /disk$web/lighttpd/db/rehearsal.sqlite3
$ DELETE DISK$WEB:[LIGHTTPD.DB]REHEARSAL.SQLITE3;*, REHEARSAL.SQLITE3-LOCK;*
```

`phpbb_mysql_to_sqlite.php` is in this repository's `contrib/`; copy it to the node (here, into the scratch directory).
It reads the MariaDB connection and the password from the board's `config.php` (they are never
printed) and stops before copying anything if the two databases' tables or columns differ.
That would mean an extension enabled in one and not the other (step 2). It ends with
`copied N rows in T tables ...; integrity_check ok`.

## 4. Stop the board

```
$ @VMSLIGHTTPD$ROOT:[COM]VMSLIGHTTPD$CONTROL STOP
```

This stops lighttpd and the PHP pool, so nothing writes to MariaDB during the copy.

## 5. Copy the data

```
$ php /disk$web/lighttpd/scratch/phpbb_mysql_to_sqlite.php /disk$web/lighttpd/htdocs/phpbb/config.php /disk$web/lighttpd/db/phpbb.sqlite3
```

Every table in the SQLite database is emptied and refilled from MariaDB, each in one
transaction. The script compares the row counts of every table and runs
`PRAGMA integrity_check`; any difference stops it with an error. The result is the board
exactly as it was: users and their passwords, posts, permissions, configuration, the search
index, the installed extensions, sessions.

## 6. Give the database to the service account

The pool runs as `LIGHTTPD`, which needs to write the database, its lock file (`-LOCK`) and the
directory (SQLite makes its journal there). PHP checks access by UIC protection, not by ACLs:

```
$ SET SECURITY/OWNER=[LIGHTTPD]/PROTECTION=(S:RWE,O:RWE,G,W) DISK$WEB:[LIGHTTPD]DB.DIR
$ SET SECURITY/OWNER=[LIGHTTPD]/PROTECTION=(S:RWED,O:RWED,G,W) DISK$WEB:[LIGHTTPD.DB]PHPBB.SQLITE3, PHPBB.SQLITE3-LOCK
$ SET SECURITY/OWNER=[LIGHTTPD]/PROTECTION=(S:RWED,O:RWED,G,W) DISK$WEB:[LIGHTTPD.DB]CONFIG.PHP_MYSQL, BOARD-MYSQL.SQL
```

(`DB.DIR` is the directory file of the database directory.)

## 7. Point the board at SQLite, and start it

Edit `DISK$WEB:[LIGHTTPD.HTDOCS.PHPBB]config.php` and replace the six database lines with these (the rest of the file stays):

```php
$dbms = 'phpbb\\db\\driver\\sqlite3';
$dbhost = '/disk$web/lighttpd/db/phpbb.sqlite3';
$dbport = '';
$dbname = '';
$dbuser = '';
$dbpasswd = '';
```

`$table_prefix` stays as it was. This is the form phpBB's own installer writes for SQLite.
Then empty phpBB's cache, which holds the old database's settings and queries, and start:

```
$ DELETE DISK$WEB:[LIGHTTPD.HTDOCS.PHPBB.CACHE.PRODUCTION...]*.*;*
$ @VMSLIGHTTPD$ROOT:[COM]VMSLIGHTTPD$CONTROL START
```

Delete the cache files rather than running `phpbbcli cache:purge` as SYSTEM: files phpBB
writes for SYSTEM would be ones the pool cannot replace. If the `DELETE` reports a
subdirectory not empty, repeat it. Also, if the site's pool `PHP.INI` sets
`opcache.validate_timestamps = 0`, delete the pool's `[.OPCACHE...]` files too, or the old
`config.php` stays compiled.

## 8. Check

- The board index loads; log in, read a topic, post a reply, search, and open the ACP.
- **ACP, General, Search settings:** if the search backend was "MySQL Fulltext", choose
  "phpBB Native Fulltext" and create its index (ACP, Maintenance, Search index). The native
  index, if that is what the board used, came across with the copy.
- The ACP's index page (board statistics) names the database server: it should say SQLite.
- Problems: `VMSLIGHTTPD$CONTROL STATUS`, PHP's error log in the pool's `[.LOGS]`, and phpBB's
  error log (ACP, Maintenance). For SQLite errors, `DEFINE/SYSTEM VMSSQLITE_ERRORS <dir>` makes
  each process log the file layer's errors with their VMS status (vms-sqlite's README); the pool
  must be restarted to see it.

## 9. Going back

MariaDB still holds the board as it was at step 4 (posts made on SQLite since are not in it):

```
$ @VMSLIGHTTPD$ROOT:[COM]VMSLIGHTTPD$CONTROL STOP
$ COPY DISK$WEB:[LIGHTTPD.DB]CONFIG.PHP_MYSQL DISK$WEB:[LIGHTTPD.HTDOCS.PHPBB]config.php
$ DELETE DISK$WEB:[LIGHTTPD.HTDOCS.PHPBB.CACHE.PRODUCTION...]*.*;*
$ @VMSLIGHTTPD$ROOT:[COM]VMSLIGHTTPD$CONTROL START
```

## Afterwards

- **Backups:** the shell's `.backup` copies a live database safely (it takes SQLite's locks), so
  the board can stay up:
  `sqlite3 /disk$web/lighttpd/db/phpbb.sqlite3 ".backup /disk$web/lighttpd/db/backup.sqlite3"`.
  A plain `COPY` or `BACKUP` of `PHPBB.SQLITE3` is safe only with the board stopped.
- **The lock file** `PHPBB.SQLITE3-LOCK` stays beside the database; never delete it while the
  board runs. To move the database, stop the board and move both.
- **No WAL:** SQLite on OpenVMS uses rollback journals; writes from the pool's processes take
  turns. phpBB's own test board on SQLite (four FastCGI processes, 30 minutes of load) answered
  more requests than the same board on MariaDB (vms-php PHASE6).
- **Cleanup**, once you're satisfied: the scratch tree, and in MariaDB the board's database
  (keep `BOARD-MYSQL.SQL`).

## Tested so far

- `phpbb_mysql_to_sqlite.php` against vms-lighttpd's phpBB 3.3.19 test board on MariaDB, into a
  database made by phpBB's installer (vms-php's SQLite test board, D29): 69 tables, 3,575 rows,
  the schemas identical, every count equal, `integrity_check` ok (2026-10-23).
- Not yet run: that board served from the copied database (`test_phpbb.py`), and the steps
  above end to end.
