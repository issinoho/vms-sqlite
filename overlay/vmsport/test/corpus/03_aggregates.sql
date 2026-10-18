-- aggregates over groups, NULLs, mixed types (the VSI C x86 SUM/AVG/TOTAL bug)
create table s(g text, i integer, r real);
insert into s values('a', 1, 0.1), ('a', 2, 0.2), ('a', null, null), ('b', 9223372036854775807, 1e308), ('b', 1, 1e308),
  ('c', -3, -0.5), ('c', 3, 0.5), ('c', 4, 2.25), ('d', null, null), ('e', 10, 1.5), ('e', 10, 2.5);
select g, sum(i), avg(i), total(i), sum(r), avg(r), total(r), count(*), count(i), min(i), max(r), group_concat(i), group_concat(r, ';') from s where g != 'b' group by g order by g;
select g, total(i), avg(i), total(r), avg(r) from s where g = 'b' group by g;
select sum(i) from s where g = 'b';
select count(distinct i), sum(distinct i), avg(distinct r), string_agg(distinct g, '') from s;
select g, sum(i) filter (where i > 1), count(*) filter (where r > 0.3) from s group by g having count(*) > 1 order by 2 desc nulls last;
select median_like from (select avg(i) as median_like from (select i from s where i is not null order by i limit 2 offset 3));
with recursive c(x) as (select 1 union all select x + 1 from c where x < 100000)
  select sum(x), avg(x), total(x), sum(x * 0.001), avg(x * 0.001), total(x * 0.001), sum(-x), avg(1.0 / x) from c;
