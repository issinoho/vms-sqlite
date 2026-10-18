-- joins, subqueries, compound selects, ordering
create table a(id integer primary key, v text);
create table b(id integer, a_id integer, w real);
insert into a values(1, 'one'), (2, 'two'), (3, 'three'), (4, null);
insert into b values(10, 1, 1.5), (11, 1, 2.5), (12, 3, 3.5), (13, 5, 4.5), (14, null, 5.5);
select a.id, a.v, b.id, b.w from a join b on a.id = b.a_id order by 1, 3;
select a.id, b.id from a left join b on a.id = b.a_id order by 1, 2;
select a.id, b.id from a right join b on a.id = b.a_id order by 2;
select a.id, b.id from a full join b on a.id = b.a_id order by coalesce(a.id, 99), b.id;
select id, (select count(*) from b where b.a_id = a.id) c, exists(select 1 from b where b.a_id = a.id and w > 2) e from a order by id;
select id from a where id in (select a_id from b) order by id;
select id from a where id not in (select a_id from b where a_id is not null) order by id;
select v from a union select cast(w as text) from b order by 1 nulls first;
select a_id from b intersect select id from a; select id from a except select a_id from b order by 1;
select id, v from a order by v desc nulls last limit 2 offset 1;
select a.v, sum(b.w) from a, b where a.id = b.a_id group by a.v order by 2 desc;
with recursive t(n) as (select 1 union all select n + 1 from t where n < 300) select count(*), sum(x.n * y.n) from t x join t y on (x.n + y.n) % 7 = 0;
