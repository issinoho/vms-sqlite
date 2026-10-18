-- strings, printf, collation, LIKE/GLOB
select substr('hello world', 2, 3), substr('hello', -3), substr('hello', 0, 2), instr('banana', 'na'), replace('aaa', 'a', 'bb'), trim('  x  '), ltrim('xxyx', 'x'), rtrim('abc  '), upper('aBc'), lower('AbC');
select length('héllo'), length(x'00ff'), octet_length('héllo'), unicode('é'), char(72, 105, 233), hex('é'), hex(123), quote('it''s'), concat('a', null, 1, 2.5), concat_ws('-', 'a', null, 'b');
select printf('[%d|%5d|%-5d|%05d|%+d]', 42, 42, 42, 42, 42), printf('[%5.2f|%e|%E|%g|%G|%10.3e]', 3.14159, 12345.678, 0.000123, 1e-5, 1e20, -2.5);
select printf('[%x|%X|%o|%c|%s|%10s|%-10s|%.2s]', 255, 255, 8, 'xyz', 'str', 'right', 'left', 'trunc'), printf('[%q|%Q|%w]', 'it''s', 'it''s', 'id"x'), printf('%,d', 1234567890);
select 'abc' like 'A%', 'abc' like 'a_c', 'a%c' like 'a\%c' escape '\', 'abc' glob 'a*', 'abc' glob 'A*', 'x.y' glob '*.?', '[x]' glob '[[]x]';
select 'abc' = 'ABC' collate nocase, 'abc ' = 'abc' collate rtrim, 'b' > 'B', 'é' > 'z';
create table w(x text collate nocase);
insert into w values('b'), ('A'), ('a'), ('B'), ('c'), (' c');
select x from w order by x, x collate binary;
select group_concat(x, ',') from (select x from w order by x collate binary desc);
select format('%s=%d', 'n', 5), likelihood(1, 0.5), unhex('48656c6c6f'), unistr('é\0041');
