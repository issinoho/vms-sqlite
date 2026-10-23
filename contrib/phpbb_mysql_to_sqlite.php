<?php
/*
** phpbb_mysql_to_sqlite.php - copy a phpBB 3.3 board's data from MySQL/MariaDB
** into an SQLite database that phpBB itself created (docs/PHPBB_MIGRATION.md).
**
**   php phpbb_mysql_to_sqlite.php <board's config.php> <target.sqlite3> [--force]
**
** The board's config.php gives the MySQL connection and the table prefix; the
** password is read from it, never typed or printed. The target must hold the
** schema of the same phpBB release with the same extensions enabled (a fresh
** install with phpbbcli, then extension:enable): its tables decide what is
** copied, and the copy stops if the two schemas differ (--force copies the
** common columns of the common tables anyway). Every target table is emptied
** and refilled, each in one transaction, then the row counts are compared and
** the database is checked with PRAGMA integrity_check.
**
** Read-only on MySQL: stop the board first, so nothing changes during the copy.
** Needs pdo_mysql and pdo_sqlite (PHP for OpenVMS 8.5.12-vms2 has both).
*/

error_reporting(E_ALL);
ini_set('display_errors', 'stderr');
ini_set('memory_limit', '512M');

function say($s) { fwrite(STDOUT, $s . "\n"); }
function die_with($s) { fwrite(STDERR, "mysql_to_sqlite: $s\n"); exit(1); }

$args = array_values(array_filter(array_slice($argv, 1), function ($a) { return $a !== '--force'; }));
$force = in_array('--force', $argv, true);
if (count($args) != 2) die_with('usage: php phpbb_mysql_to_sqlite.php <config.php> <target.sqlite3> [--force]');
list($config, $target) = $args;

// config.php only sets variables and defines constants
if (!is_readable($config)) die_with("cannot read $config");
$dbms = $dbhost = $dbport = $dbname = $dbuser = $dbpasswd = $table_prefix = null;
include $config;
if (strpos((string) $dbms, 'mysql') === false) die_with("$config: \$dbms is '$dbms', not a MySQL driver");
if (!file_exists($target)) die_with("$target does not exist: create it with phpBB's installer first");

// TCP only: "localhost" would mean a Unix-domain socket, which OpenVMS lacks
$host = $dbhost === '' || $dbhost === null || $dbhost === 'localhost' ? '127.0.0.1' : $dbhost;
$dsn = "mysql:host=$host;dbname=$dbname;charset=utf8";      // phpBB's mysqli driver: SET NAMES 'utf8'
if ($dbport !== '' && $dbport !== null) $dsn .= ";port=$dbport";
try {
    $my = new PDO($dsn, $dbuser, $dbpasswd, [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
        PDO::MYSQL_ATTR_USE_BUFFERED_QUERY => false,        // stream big tables
    ]);
    $lite = new PDO("sqlite:$target", null, null, [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);
} catch (PDOException $e) {
    die_with('connect: ' . $e->getMessage());
}
$lite->exec('PRAGMA busy_timeout = 60000');
say("from MySQL $host/$dbname, prefix $table_prefix, to $target (SQLite " .
    $lite->query('select sqlite_version()')->fetchColumn() . ')');

$like = str_replace(['\\', '_', '%'], ['\\\\', '\\_', '\\%'], $table_prefix) . '%';
$myTables = [];
$st = $my->query('SHOW TABLES LIKE ' . $my->quote($like));
foreach ($st->fetchAll(PDO::FETCH_COLUMN) as $t) $myTables[$t] = true;
$st->closeCursor();
$liteTables = [];
$st = $lite->query("SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\' ORDER BY name");
foreach ($st->fetchAll(PDO::FETCH_COLUMN) as $t) if (strpos($t, $table_prefix) === 0) $liteTables[$t] = true;
if (!$liteTables) die_with("$target has no {$table_prefix}* tables");

// compare the schemas: tables, then columns
$problems = [];
foreach (array_diff_key($myTables, $liteTables) as $t => $_) $problems[] = "table $t is only in MySQL (an extension not enabled in the target?)";
foreach (array_diff_key($liteTables, $myTables) as $t => $_) $problems[] = "table $t is only in SQLite";
$plan = [];
foreach (array_intersect_key($liteTables, $myTables) as $t => $_) {
    $lcols = [];
    foreach ($lite->query("PRAGMA table_info(\"$t\")") as $c) $lcols[$c['name']] = strtoupper($c['type']);
    $mcols = [];
    $st = $my->query("SHOW COLUMNS FROM `$t`");
    foreach ($st->fetchAll(PDO::FETCH_ASSOC) as $c) $mcols[$c['Field']] = true;
    $st->closeCursor();
    foreach (array_diff_key($mcols, $lcols) as $c => $_) $problems[] = "column $t.$c is only in MySQL";
    foreach (array_diff_key($lcols, $mcols) as $c => $_) $problems[] = "column $t.$c is only in SQLite";
    $cols = array_intersect_key($lcols, $mcols);
    if ($cols) $plan[$t] = $cols;
}
foreach ($problems as $p) say(($force ? 'warning: ' : 'error: ') . $p);
if ($problems && !$force) die_with(count($problems) . ' schema differences; enable the same extensions in the target, or --force');

$total = 0;
$t0 = microtime(true);
foreach ($plan as $t => $cols) {
    $names = array_keys($cols);
    $list = '"' . implode('", "', $names) . '"';
    $ins = $lite->prepare("INSERT INTO \"$t\" ($list) VALUES (" . implode(', ', array_fill(0, count($names), '?')) . ')');
    $lob = [];
    foreach ($names as $i => $c) $lob[$i] = strpos($cols[$c], 'BLOB') !== false;
    $lite->beginTransaction();
    $lite->exec("DELETE FROM \"$t\"");
    $sel = $my->query('SELECT `' . implode('`, `', $names) . "` FROM `$t`");
    $n = 0;
    while (($row = $sel->fetch(PDO::FETCH_NUM)) !== false) {
        foreach ($row as $i => $v) {
            if ($v === null) $ins->bindValue($i + 1, null, PDO::PARAM_NULL);
            else $ins->bindValue($i + 1, $v, $lob[$i] ? PDO::PARAM_LOB : PDO::PARAM_STR);
        }
        $ins->execute();
        $n++;
    }
    $sel->closeCursor();
    $lite->commit();
    $mc = (int) $my->query("SELECT COUNT(*) FROM `$t`")->fetchColumn();
    $lc = (int) $lite->query("SELECT COUNT(*) FROM \"$t\"")->fetchColumn();
    if ($mc !== $lc) die_with("$t: $mc rows in MySQL, $lc in SQLite");
    say(sprintf('%-40s %8d rows', $t, $n));
    $total += $n;
}
$check = $lite->query('PRAGMA integrity_check')->fetchAll(PDO::FETCH_COLUMN);
if ($check !== ['ok']) die_with('integrity_check: ' . implode('; ', $check));
say(sprintf('copied %d rows in %d tables in %.1f s; integrity_check ok', $total, count($plan), microtime(true) - $t0));
exit(0);
