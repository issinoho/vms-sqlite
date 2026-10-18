-- type affinity, typeof, casts, comparisons
create table t(i integer, r real, x text, b blob, n numeric, a);
insert into t values(1, 1, 1, 1, 1, 1), ('2', '2.5', 2.5, x'0102', '3.0', '4'), (null, null, null, null, null, null),
  (9223372036854775807, 1e300, 'abc', x'', '1e3', 1.5), (-9223372036854775808, -0.0, '', 'txt', '12abc', x'ff');
select typeof(i), typeof(r), typeof(x), typeof(b), typeof(n), typeof(a), quote(i), quote(r), quote(x), quote(b), quote(n), quote(a) from t;
select cast('123abc' as integer), cast('1.5e2' as real), cast(12.9 as integer), cast(-12.9 as integer), cast(x'414243' as text), cast('9223372036854775808' as integer), cast(1e19 as integer);
select 1 < '1', 1 = '1', '10' < '9', 10 < 9, x'00' < '', null = null, null is null, 1 is not null;
select 9223372036854775807 + 1, -9223372036854775808 - 1, 9223372036854775807 * 2, 5 / 2, 5.0 / 2, -5 / 2, -5 % 3, 5 % -3, 1/0, 1%0;
select 0.1 + 0.2, 1e-320, 2.2250738585072014e-308, 1.7976931348623157e308, -1e308 * 10, 3.0, 100.0, 1e15, 1e16, 123456789012345678;
select printf('%!.20g', 0.1), printf('%.17g', 1.0/3), printf('%a', 1.0), 4.9406564584124654e-324;
