#pragma once

#include "pico/divider.h"


using fix15 = signed int;

constexpr fix15 int2fix15(int a) { return a << 15; }

constexpr float fix2float15(fix15 a) {
    return static_cast<float>(a) / 32768.0f;
}

constexpr fix15 float2fix15(float a) { return static_cast<fix15>(a * 32768.0); } // 2^15
inline fix15 divfix(fix15 a, fix15 b) {
    return static_cast<fix15>(div_s64s64((static_cast<signed long long>(a) << 15), b));
}

constexpr fix15 multfix15(fix15 a, fix15 b) {
    return static_cast<fix15>((static_cast<signed long long>(a) * static_cast<signed long long>(b)) >> 15);
}

constexpr fix15 absfix15(fix15 a) { return a >= 0 ? a : -a; }
constexpr int fix2int15(fix15 a) { return a >> 15; }
constexpr fix15 char2fix15(char a) { return static_cast<fix15>(a) << 15; }

constexpr fix15 fix15norm(fix15 a, fix15 b) {
    a = a >= 0 ? a : -a;
    b = b >= 0 ? b : -b;
    fix15 maxVal = a > b ? a : b;
    fix15 minVal = a > b ? b : a;
    return maxVal + ((minVal * 3) >> 3); // max + min*0.375
}