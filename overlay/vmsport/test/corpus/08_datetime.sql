-- dates and times (fixed instants only: no 'now', no localtime)
select date('2026-10-18'), time('13:45:30.123'), datetime('2026-10-18 13:45:30'), julianday('2026-10-18 12:00'), unixepoch('2026-10-18'), datetime(1760000000, 'unixepoch');
select date('2026-01-31', '+1 month'), date('2026-03-31', '-1 month'), date('2024-02-29', '+1 year'), date('2026-10-18', 'start of month'), date('2026-10-18', 'start of year', '+59 days'), date('2026-10-18', 'weekday 0'), date('2026-10-18', '+7 days', 'weekday 1');
select strftime('%Y-%m-%d %H:%M:%S %j %w %W %s %f %J', '2026-10-18 07:08:09.5'), strftime('%u %G %V %e %k %l %I %p %P %R %T %F %D', '2026-01-01 15:04:05');
select datetime('2026-10-18 23:59:59', '+1 second'), datetime(2461334.5), datetime('2026-10-18T10:00:00Z'), datetime('2026-10-18 10:00:00+05:30'), datetime(0, 'unixepoch'), datetime(-1, 'unixepoch'), datetime(4102444800, 'unixepoch');
select timediff('2026-10-18', '2025-01-01'), timediff('2026-10-18 12:00', '2026-10-18 10:30:15.25'), datetime('2026-10-18 12:00:00.123456', 'subsec'), julianday('0000-01-01'), date(5373484.4);
select date('2026-02-30'), date('garbage'), time('25:00'), date(null), datetime('2038-01-19 03:14:08'), unixepoch('2038-01-19 03:14:08'), datetime(2147483648, 'unixepoch'), unixepoch('2106-02-07 06:28:16');
