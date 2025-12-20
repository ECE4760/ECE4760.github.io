/** fix15.h
 * Macros for fixed-point operations.
 */

#ifndef __FIX15__H__
#define __FIX15__H__

#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include "pico/float.h"
#include "pico/double.h"
#include "pico/rand.h"

// 32-bit word with 21 fractional bits (4.76837158203125e-7 resolution)
typedef int32_t fix21;

// Type constants
#define FIX21_MAX         INT32_MAX
#define FIX21_MIN         INT32_MIN
#define INT2FIX21_MAX     1023
#define INT2FIX21_MIN     -1024

// Useful, pre-converted literals
#define PI21              6588397     // pi       ( <== 3.141592502593994     )
#define TAU21             13176795    // 2 * pi   ( <== 6.2831854820251465    )
#define PI_O_TWO21        3294199     // pi/2     ( <== 1.5707964897155762    )
#define PI_O_FOUR21       1647099     // pi/4     ( <== 0.785398006439209     )
#define PI_O_18021        36602       // pi/180   ( <== 0.01745319366455078   )
#define ONEEIGHTY_O_PI21  120157959   // 180/pi   ( <== 57.29577970504761     )
#define ONE_O_PI21        667544      // 1/pi     ( <== 0.3183097839355469    )
#define ONE21             2097152     // 1        ( <== 1                     )
#define PTONE21           209715      // 0.1      ( <== 0.09999990463256836   )
#define PTZEROONE21       20972       // 0.01     ( <== 0.010000228881835938  )
#define PTZEROZEROONE21   2097        // 0.001    ( <== 0.0009999275207519531 )
#define FIX21_UNIT        1           // 2^{-21}  ( <== 4.76837158203125e-7   )

// fix21 conversions
#define int2fix21(a) ((fix21)((a) << 21))
#define char2fix21(a) ((fix21)(((fix21)(a)) << 21))
#define fix212int(a) ((int)((a) >> 21))

// Using SDK conversions
#define float2fix21(a) ((fix21)float2fix_z((a), 21))
#define fix212float(a) (fix2float((a), 21))
#define double2fix21(a) ((fix21)double2fix_z((a), 21))
#define fix212double(a) (fix2double((a), 21))

// Math macros (can use normal abs())
#define multfix21(a, b)                                                        \
((fix21)((((int64_t)(a)) * ((int64_t)(b))) >> 21))
#define divfix21(a, b) ((fix21)((((int64_t)(a)) << 21) / (b)))
#define modfix21(a, b) ((fix21)((((int64_t)(a)) << 21) % (b)))
#define sqrtfix21(a) (float2fix21(sqrtf(fix212float(a))))

#define rad_to_deg21(a) (multfix21((a), ONEEIGHTY_O_PI21))
#define deg_to_rad21(a) (multfix21((a), PI_O_18021))

// RNG helpers

// Random fixed-point number in range of [0, 1)
#define rand_fix21_01() ((fix21)(get_rand_32() & (0x1fffff)))
// Random fixed-point number in range of [-1,1]
#define rand_fix21_11() ((((fix21)get_rand_32()) >> 21) - ONE21) // Convert to signed, right-shift w/ sign
// Random fixed-point number in range of [0, 2)
#define rand_fix21_02() ((fix21)(get_rand_32() >> 21)) // Get u32, keep signed, shift to [0, 2), then convert
// Random positive fixed-point number (and zero)
#define rand_fix21_pos() ((fix21)(get_rand_32() & FIX21_MAX))
// Random negative fixed-point number (and zero)
#define rand_fix21_neg() ((fix21)((get_rand_32() & FIX21_MAX) | 0x80000000))
// Random fixed-point number in entire fix21 range
#define rand_fix21_all() ((fix21)(get_rand_32()))
// Random fixed-point number in range [0, 2pi]
#define rand_fix21_2pi() (multfix21(rand_fix21_02(), (PI21)))
// Random fixed-point number in range [-pi, pi]
#define rand_fix21_pipi() (multfix21(rand_fix21_11(), (PI21)))
// Random fixed-point number in range [a, b]
// Get random fractional part, XOR with separate factional, then scale to range
#define rand_fix21(a, b) ((multfix21((rand_fix21_01() ^ (fix21)(get_rand_32() >> 10)), ((b) - (a))) + (a)))


// 32-bit word with 15 fractional bits (0.000030517578125 resolution)
typedef int32_t fix15;

// Type constants
#define FIX15_MAX         INT32_MAX
#define FIX15_MIN         INT32_MIN
#define INT2FIX15_MAX     65535
#define INT2FIX15_MIN     -1024

// Useful, pre-converted literals
#define PI15              102944  // pi      ( <== 3.1416015625      )
#define TAU15             205888  // 2 * pi  ( <== 6.283203125       )
#define PI_O_TWO15        51472   // pi/2    ( <== 1.57080078125     )
#define PI_O_FOUR15       25736   // pi/4    ( <== 0.785400390625    )
#define PI_O_18015        572     // pi/180  ( <== 0.0174560546875   )
#define ONEEIGHTY_O_PI15  1877468 // 180/pi  ( <== 57.2957763671875  )
#define ONE_O_PI15        10430   // 1/pi    ( <== 0.31829833984375  )
#define ONE15             32768   // 1       ( <== 1                 )
#define PTONE15           3277    // 0.1     ( <== 0.100006103515625 )
#define PTZEROONE15       328     // 0.01    ( <== 0.010009765625    )
#define FIX15_UNIT        1       // 2^{-15} ( <== 0.000030517578125 )


// fix15 conversions
#define int2fix15(a) ((fix15)((a) << 15))
#define char2fix15(a) ((fix15)(((fix15)(a)) << 15))
#define fix152int(a) ((int)((a) >> 15))
// Using SDK conversions
#define float2fix15(a) ((fix15)float2fix_z((a), 15))
#define fix152float(a) (fix2float((a), 15))

// Math macros (can use normal abs())
#define multfix15(a, b)                                                        \
((fix15)((((int64_t)(a)) * ((int64_t)(b))) >> 15))
#define divfix15(a, b) ((fix15)((((int64_t)(a)) << 15) / (b)))
#define modfix15(a, b) ((fix15)((((int64_t)(a)) << 15) % (b)))
#define sqrtfix15(a) (float2fix15(sqrtf(fix152float(a))))

#define rad_to_deg15(a) (multfix15((a), ONEEIGHTY_O_PI15))
#define deg_to_rad15(a) (multfix15((a), PI_O_18015))

// RNG helpers

// Random fixed-point number in range of [0, 1)
#define rand_fix15_01() ((fix15)(get_rand_32() & 0x7fff))
// Random fixed-point number in range of [-1,1]
#define rand_fix15_11() ((((fix15)get_rand_32()) >> 15) - ONE15) // Convert to signed, right-shift w/ sign
// Random fixed-point number in range of [0, 2)
#define rand_fix15_02() ((fix15)(get_rand_32() >> 15)) // Get u32, keep signed, shift to [0, 2), then convert
// Random positive fixed-point number (and zero)
#define rand_fix15_pos() ((fix15)(get_rand_32() & 0x7fffffff))
// Random negative fixed-point number (and zero)
#define rand_fix15_neg() ((fix15)((get_rand_32() & 0x7fffffff) | 0x80000000))
// Random fixed-point number in entire fix15 range
#define rand_fix15_all() ((fix15)(get_rand_32()))
// Random fixed-point number in range [0, 2pi]
#define rand_fix15_2pi() (multfix15(rand_fix15_02(), (PI15)))
// Random fixed-point number in range [a, b]
#define rand_fix15(a, b) ((multfix15((rand_fix15_01() ^ (fix15)(get_rand_32() >> 16)), ((b) - (a))) + (a)))


// 32-bit word with 8 fractional bits (0.00390625 resolution)
typedef int32_t fix8;

// Type constants
#define FIX8_MAX          INT32_MAX
#define FIX8_MIN          INT32_MIN
#define INT2FIX8_MAX      8388607
#define INT2FIX8_MIN      -8388608

// No other constants defined due to lack of use

// fix8 conversions
#define int2fix8(a) ((fix8)((a) << 8))
#define char2fix8(a) (fix8)(((fix8)(a)) << 8)
#define fix82int(a) ((int)((a) >> 8))
// Using SDK conversions
#define float2fix8(a) ((fix8)float2fix_z((a), 8))
#define fix82float(a) (fix2float((a), 8))

// Math macros (can use normal abs())
#define multfix8(a, b)                                                         \
((fix8)((((int64_t)(a)) * ((int64_t)(b))) >> 8))
#define divfix8(a, b) (fix8)((((int64_t)(a)) << 8) / (b))
#define modfix8(a, b) ((fix8)((((int64_t)(a)) << 8) % (b)))
#define sqrtfix8(a) (float2fix8(sqrtf(fix82float(a))))


// Fix to fix conversions (these do NOT round and lose precision quickly)
// fix21 -> ...
#define fix212fix15(a) (fix15)((fix21)(a) >> 6)
#define fix212fix8(a) (fix8)((fix21)(a) >> 13)
// fix15 -> ...
#define fix152fix21(a) (fix21)((fix15)(a) << 6)
#define fix152fix8(a) (fix8)((fix15)(a) >> 7)
// fix8 -> ...
#define fix82fix15(a) (fix15)((fix8)(a) << 7)
#define fix82fix21(a) (fix21)((fix8)(a) << 13)



// Fixed-point functions

// fix21
fix21 sinfx21(fix21 r);
fix21 cosfx21(fix21 r);
fix21 tanfx21(fix21 r);
fix21 asinfx21(fix21 r);
fix21 acosfx21(fix21 r);
fix21 atan2fx21(fix21 a, fix21 b);
void  sincosfx21(fix21 r, fix21 *sin, fix21 *cos);

fix21 floorfx21(fix21 r);
fix21 ceilfx21(fix21 r);

// fix15
fix15 sinfx15(fix15 r);
fix15 cosfx15(fix15 r);
fix15 tanfx15(fix15 r);
fix15 asinfx15(fix15 r);
fix15 acosfx15(fix15 r);
fix15 atan2fx15(fix15 a, fix15 b);
void  sincosfx15(fix15 r, fix15 *sin, fix15 *cos);

fix15 floorfx15(fix15 r);
fix15 ceilfx15(fix15 r);

uint32_t log2i(uint32_t i);


#endif  //!__FIX15__H__