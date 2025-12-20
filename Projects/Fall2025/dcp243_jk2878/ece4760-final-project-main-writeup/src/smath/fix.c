/** fix15.c
 * Macros for fixed-point operations. Most of these are simply calling the
 * appropriate
 */

#include "fix.h"
#include <stdint.h>

// All just converting to/from floats for now

fix21 sinfx21(fix21 r) { return double2fix21(sin(fix212double(r))); }

fix21 cosfx21(fix21 r) { return double2fix21(cos(fix212double(r))); }

fix21 tanfx21(fix21 r) { return double2fix21(tan(fix212double(r))); }

fix21 asinfx21(fix21 r) { return double2fix21(asin(fix212double(r))); }

fix21 acosfx21(fix21 r) { return double2fix21(acos(fix212double(r))); }

fix21 atan2fx21(fix21 a, fix21 b) {
  return double2fix21(atan2(fix212double(a), fix212double(b)));
}

void sincosfx21(fix21 r, fix21 *sin, fix21 *cos) {
  double sin_f, cos_f;

  sincos(fix212double(r), &sin_f, &cos_f);
  *sin = double2fix21(sin_f);
  *cos = double2fix21(cos_f);
}

fix21 floorfx21(fix21 r) {
  // Truncate decimal while keeping type
  return (r & 0xffe00000);
}

fix21 ceilfx21(fix21 r) {
  // Truncate decimal while keeping type and adding 1 if needed
  if (r & 0x1fffff) {
    r += ONE21; // Check for decimal and add one if present
  }

  return (r & 0xffe00000);
}

fix15 sinfx15(fix15 r) { return float2fix15(sinf(fix152float(r))); }

fix15 cosfx15(fix15 r) { return float2fix15(cosf(fix152float(r))); }

fix15 tanfx15(fix15 r) { return float2fix15(tanf(fix152float(r))); }

fix15 asinfx15(fix15 r) { return float2fix15(asinf(fix152float(r))); }

fix15 acosfx15(fix15 r) { return float2fix15(acosf(fix152float(r))); }

fix15 atan2fx15(fix15 a, fix15 b) {
  return float2fix15(atan2f(fix152float(a), fix152float(b)));
}

void sincosfx15(fix15 r, fix15 *sin, fix15 *cos) {
  float sin_f, cos_f;

  sincosf(fix152float(r), &sin_f, &cos_f);
  *sin = float2fix15(sin_f);
  *cos = float2fix15(cos_f);
}

fix15 floorfx15(fix15 r) {
  // Truncate decimal while keeping type
  return (r & 0xffff8000);
}

fix15 ceilfx15(fix15 r) {
  // Truncate decimal while keeping type and adding 1 if needed
  if (r & 0x7fff) {
    r += ONE15; // Check for decimal and add one if present
  }

  return (r & 0xffff8000);
}

// Source: https://graphics.stanford.edu/~seander/bithacks.html#IntegerLog
uint32_t log2i(uint32_t v) {
  const uint32_t b[] = {0x2, 0xC, 0xF0, 0xFF00, 0xFFFF0000};
  const uint32_t S[] = {1, 2, 4, 8, 16};
  int i;

  register uint32_t r = 0; // result of log2(v) will go here
  for (i = 4; i >= 0; i--)     // unroll for speed...
  {
    if (v & b[i]) {
      v >>= S[i];
      r |= S[i];
    }
  }

  return r;
}

// Fifth-order approximation of sin(x)
// See
// https://www.nullhardware.com/blog/fixed-point-sine-and-cosine-for-embedded-systems/
// and https://www.coranac.com/2009/07/sines/ for more details

// TODO: Implement variation of this if needed
// fix15 sinfx(fix15 r) {
//   // Convert fix15 input into int16_t with 2^15 units/circle
//   uint_16t i =

//   /*
//   Implements the 5-order polynomial approximation to sin(x).
//   @param i   angle (with 2^15 units/circle)
//   @return    16 bit fixed point Sine value (4.12) (ie: +4096 = +1 & -4096 =
//   -1)

//   The result is accurate to within +- 1 count. ie: +/-2.44e-4.
//   */

//   /* Convert (signed) input to a value between 0 and 8192. (8192 is pi/2,
//   which is the region of the curve fit). */
//   /* ------------------------------------------------------------------- */
//   i <<= 1;
//   uint8_t c = i<0; //set carry for output pos/neg

//   if(i == (i|0x4000)) // flip input value to corresponding value in range
//   [0..8192)
//       i = (1<<15) - i;
//   i = (i & 0x7FFF) >> 1;
//   /* ------------------------------------------------------------------- */

//   /* The following section implements the formula:
//     = y * 2^-n * ( A1 - 2^(q-p)* y * 2^-n * y * 2^-n * [B1 - 2^-r * y * 2^-n
//     * C1 * y]) * 2^(a-q)
//   Where the constants are defined as follows:
//   */
//   enum {A1=3370945099UL, B1=2746362156UL, C1=292421UL};
//   enum {n=13, p=32, q=31, r=3, a=12};

//   uint32_t y = (C1*((uint32_t)i))>>n;
//   y = B1 - (((uint32_t)i*y)>>r);
//   y = (uint32_t)i * (y>>n);
//   y = (uint32_t)i * (y>>n);
//   y = A1 - (y>>(p-q));
//   y = (uint32_t)i * (y>>n);
//   y = (y+(1UL<<(q-a-1)))>>(q-a); // Rounding

//   y =  c ? -y : y;

//   return y;
// }