/* p_sqrt.c - a correctly rounded sqrt() for VSI C (corpus: sqrt(2) one unit
   off, PORTING_LOG #18; patch 0005 covers clang with __builtin_sqrt only).
   The C RTL's r = sqrt(x) is taken to be within one unit of the true root;
   then x is compared with the squares of the midpoints between r and its
   neighbours.  A midpoint has 54 significant bits, its square at most 108,
   so with an IEEE quad long double (113 bits) the squares and the
   comparisons are exact.  (A root is never exactly a midpoint.)
     S1  long double: LDBL_MANT_DIG (needs >= 108)
     S2  sqrt(2) from the C RTL, and corrected
     S3  2,000,000 random positive doubles (all exponents, and near 1):
         how many the C RTL gets wrong, and (clang) how many corrected
         results differ from the SSE instruction (must be 0)
     S4  cost per call, C RTL and corrected */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

static double rtl_sqrt(double x) { return sqrt(x); }
static double (*volatile rtl)(double) = rtl_sqrt;       /* not the instruction */

static double fixed_sqrt(double x)
{
  double r = rtl(x);
  if (x > 0 && x <= DBL_MAX) {
    double dn = nextafter(r, 0), up = nextafter(r, DBL_MAX);
    long double lx = x;
    long double lo = ((long double) dn + r) / 2, hi = ((long double) r + up) / 2;
    if (lx < lo * lo) r = dn;
    else if (lx > hi * hi) r = up;
  }
  return r;
}

static unsigned long long seed = 88172645463325252ULL;
static double rnd(int nearOne)
{
  unsigned long long b;
  double d;
  do {
    seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
    b = seed & 0x7FFFFFFFFFFFFFFFULL;                 /* positive */
    if (nearOne) b = (b & 0x000FFFFFFFFFFFFFULL) | (((unsigned long long) (1022 + (seed >> 63)) & 0x7FF) << 52);
    memcpy(&d, &b, sizeof d);
  } while (!(d > 0 && d <= DBL_MAX));
  return d;
}

#ifdef __clang__
static double sse_sqrt(double x)   /* the instruction itself: __builtin_sqrt calls the C RTL without -fno-math-errno */
{
  double r;
  __asm__("sqrtsd %1, %0" : "=x"(r) : "x"(x));
  return r;
}
#endif

static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec * 1e6 + t.tv_usec; }

int main(void)
{
  long i, nRtl = 0, nFix = 0;
  double t0, s = 0;
  printf("S1   LDBL_MANT_DIG %d (needs >= 108)\n", LDBL_MANT_DIG);
  printf("S2   sqrt(2): C RTL %.17g, corrected %.17g\n", rtl(2.0), fixed_sqrt(2.0));
#ifdef __clang__
  printf("     sqrtsd %.17g\n", sse_sqrt(2.0));
#endif
  for (i = 0; i < 2000000; i++) {
    double x = rnd(i & 1), f = fixed_sqrt(x), r = rtl(x);
    if (f != r) nRtl++;
#ifdef __clang__
    if (f != sse_sqrt(x)) {
      if (nFix++ < 5) printf("     differs: x %.17g corrected %.17g sse %.17g\n", x, f, sse_sqrt(x));
    }
#endif
  }
  printf("S3   2000000 inputs: C RTL wrong in %ld", nRtl);
#ifdef __clang__
  printf("; corrected differs from the SSE instruction in %ld\n", nFix);
#else
  printf(" (no instruction to compare with under VSI C)\n");
#endif
  t0 = now();
  for (i = 0; i < 200000; i++) s += rtl(i + 0.5);
  printf("S4   C RTL sqrt %.3f us", (now() - t0) / 200000);
  t0 = now();
  for (i = 0; i < 200000; i++) s += fixed_sqrt(i + 0.5);
  printf(", corrected %.3f us (%g)\n", (now() - t0) / 200000, s);
  return 0;
}
