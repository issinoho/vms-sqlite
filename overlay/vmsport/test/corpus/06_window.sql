-- window functions
create table e(dept text, name text, sal integer);
insert into e values('a','x',100),('a','y',200),('a','z',200),('b','p',50),('b','q',75),('c','r',300),('a','w',null);
select dept, name, sal, row_number() over w, rank() over w, dense_rank() over w, percent_rank() over w, cume_dist() over w, ntile(2) over w
  from e window w as (partition by dept order by sal desc) order by dept, sal desc, name;
select name, sal, lag(sal) over (order by name), lead(sal, 2, -1) over (order by name), first_value(name) over (order by sal), last_value(name) over (order by sal rows between unbounded preceding and unbounded following), nth_value(name, 2) over (order by name) from e order by name;
select name, sum(sal) over (order by name rows between 1 preceding and 1 following), avg(sal) over (partition by dept), count(*) over (order by sal range between 50 preceding and current row), total(sal) over (order by name groups between 1 preceding and current row exclude current row) from e order by name;
with recursive c(i) as (select 1 union all select i + 1 from c where i < 2000)
  select sum(s), max(s), min(a) from (select sum(i) over (order by i rows 10 preceding) s, avg(i * 0.5) over (order by i rows between 3 preceding and 3 following) a from c);
