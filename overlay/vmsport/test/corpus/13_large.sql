-- large values (overflow pages), many rows, many pages, page size changes
pragma page_size = 1024; vacuum;
create table big(id integer primary key, b blob, t text);
with recursive c(i) as (select 1 union all select i + 1 from c where i < 200)
  insert into big select i, zeroblob(i * 997), replace(hex(zeroblob(i * 50)), '00', char(65 + i % 26)) from c;
select count(*), sum(length(b)), sum(length(t)), max(length(t)), hex(substr(b, 1000, 4)), substr(t, 1, 5) from big;
update big set t = t || t where id % 3 = 0;
delete from big where id % 7 = 0;
select count(*), sum(length(t)), total(id) from big;
create table many(k integer primary key, v text, w real);
with recursive c(i) as (select 1 union all select i + 1 from c where i < 100000)
  insert into many select i, printf('%08x', i * 2654435761 % 4294967296), i / 7.0 from c;
create index many_v on many(v);
select count(*), sum(k), total(w), min(v), max(v) from many;
select k from many where v between '80000000' and '8000ffff' order by v limit 5;
delete from many where k % 3 = 0; select count(*) from many; pragma page_count;
pragma page_size = 8192; vacuum; pragma page_size; pragma page_count; pragma integrity_check;
