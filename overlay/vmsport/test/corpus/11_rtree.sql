-- R*Tree
create virtual table rt using rtree(id, x0, x1, y0, y1);
with recursive c(i) as (select 1 union all select i + 1 from c where i < 5000)
  insert into rt select i, (i * 37 % 1000), (i * 37 % 1000) + (i % 10), (i * 91 % 1000), (i * 91 % 1000) + (i % 7) from c;
select count(*) from rt where x0 >= 100 and x1 <= 200 and y0 >= 300 and y1 <= 500;
select count(*), sum(id) from rt where x1 >= 500 and x0 <= 510;
select id, x0, x1, y0, y1 from rt where id in (1, 2, 4999) order by id;
create virtual table rt2 using rtree_i32(id, a, b);
insert into rt2 values(1, -5, 5), (2, 0, 100), (3, 2147483000, 2147483647);
select id from rt2 where a <= 3 and b >= 3 order by id; select * from rt2 where a > 1000;
select rtreecheck('rt'), rtreecheck('rt2');
