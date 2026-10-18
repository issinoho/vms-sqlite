-- recursive CTEs: Fibonacci, a tree, the Mandelbrot set (floating point)
with recursive f(n, a, b) as (select 1, 0, 1 union all select n + 1, b, a + b from f where n < 90) select n, a from f where n in (1, 10, 50, 89, 90);
create table tree(id integer primary key, parent integer, name text);
insert into tree values(1, null, 'root'), (2, 1, 'a'), (3, 1, 'b'), (4, 2, 'a1'), (5, 2, 'a2'), (6, 5, 'a2x'), (7, 3, 'b1');
with recursive p(id, path, depth) as (select id, name, 0 from tree where parent is null union all select t.id, p.path || '/' || t.name, depth + 1 from tree t join p on t.parent = p.id)
  select * from p order by path;
WITH RECURSIVE
  xaxis(x) AS (VALUES(-2.0) UNION ALL SELECT x+0.05 FROM xaxis WHERE x<1.2),
  yaxis(y) AS (VALUES(-1.0) UNION ALL SELECT y+0.1 FROM yaxis WHERE y<1.0),
  m(iter, cx, cy, x, y) AS (
    SELECT 0, x, y, 0.0, 0.0 FROM xaxis, yaxis
    UNION ALL
    SELECT iter+1, cx, cy, x*x-y*y + cx, 2.0*x*y + cy FROM m WHERE (x*x + y*y) < 4.0 AND iter<28
  ),
  m2(iter, cx, cy) AS (SELECT max(iter), cx, cy FROM m GROUP BY cx, cy),
  a(t) AS (SELECT group_concat( substr(' .+*#', 1+min(iter/7,4), 1), '') FROM m2 GROUP BY cy)
SELECT group_concat(rtrim(t),x'0a') FROM a;
with recursive digits(n, s) as (select 1, '1' union all select n + 1, s || (n + 1) % 10 from digits where n < 500) select length(s), substr(s, 490) from digits where n = 500;
