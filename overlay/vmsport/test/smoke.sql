.vfsname
select sqlite_version();
pragma page_size;
create table t(id integer primary key, v text, n real);
begin;
with recursive c(i) as (select 1 union all select i+1 from c where i<20000)
  insert into t(v, n) select printf('row %d', i), i * 0.5 from c;
commit;
select count(*), sum(n), min(v), max(id) from t;
update t set n = n + 1 where id % 3 = 0;
delete from t where id % 10 = 0;
select count(*), total(n) from t;
create index t_v on t(v);
select v from t where v = 'row 12345';
pragma integrity_check;
pragma page_count;
