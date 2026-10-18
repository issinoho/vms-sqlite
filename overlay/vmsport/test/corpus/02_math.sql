-- arithmetic, bit operations, math functions
select 7 & 3, 7 | 8, ~0, 1 << 62, 1 << 63, 1 << 64, -1 >> 1, 255 >> 4, -9223372036854775808 / -1;
select abs(-5), abs(-5.5), round(2.5), round(-2.5), round(2.675, 2), round(1234.5678, -2), round(0.5), round(1.4999999999999999);
select sqrt(2), pow(2, 0.5), exp(1), ln(10), log(100), log10(1000), log2(1024), log(2, 8), pi();
select sin(1), cos(1), tan(1), asin(0.5), acos(0.5), atan(1), atan2(1, -1), sinh(1), cosh(1), tanh(0.5), asinh(1), atanh(0.5);
-- libm, not correctly rounded on every platform: glibc and the VMS C RTL may differ in the last bit
select acosh(2);
select ceil(1.2), ceil(-1.2), floor(1.8), floor(-1.8), trunc(-2.7), trunc(2.7), mod(10, 3), mod(-10.5, 3), degrees(pi()), radians(180), sign(-3), sign(0), sign(2.5);
select sqrt(-1), ln(0), pow(0, 0), pow(-8, 1.0/3), 1e308 + 1e308, -(1e308 + 1e308);
with recursive c(i) as (select 1 union all select i + 1 from c where i < 1000)
  select sum(i * i), sum(1.0 / i), total(sqrt(i)), avg(i * 1.5), max(sin(i)), min(cos(i)) from c;
