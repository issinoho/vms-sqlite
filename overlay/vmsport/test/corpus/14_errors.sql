-- errors: messages and statement atomicity
create table u(a integer primary key, b text unique, c integer not null check(c < 10));
insert into u values(1, 'x', 1);
insert into u values(1, 'y', 2);
insert into u values(2, 'x', 2);
insert into u values(3, 'z', null);
insert into u values(4, 'w', 11);
insert into u values(5, 'v', 1), (6, 'v', 1);
select count(*) from u;
selec 1;
select * from nosuch;
select abs();
select 1 +;
create table u(x);
begin; insert into u values(7, 'q', 1); rollback; select count(*) from u;
begin; savepoint s1; insert into u values(8, 'r', 1); rollback to s1; insert into u values(9, 's', 2); release s1; commit; select a from u order by a;
select sum(9223372036854775807 + 0) from (select 1 union all select 2);
select json('{bad');
select 'end';
