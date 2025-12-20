/** linalg.h
 * Various linear algebra structs and helpers.
 */

#ifndef __LINALG__H__
#define __LINALG__H__
#include "fix.h"
#include "pico/stdlib.h"
#include "pico/float.h"
#include "pico/rand.h"
#include "stdint.h"
#include "string.h"
  
// Homogenous 2D vector (x : y : w)
typedef union hvec2d {
  struct {
    fix15 x;
    fix15 y;
    fix15 w;
  };

  fix15 data[3];
} hvec2d_t;

typedef union hmatrix2d {
  struct {
    fix15 m00,m01,m02;
    fix15 m10,m11,m12;
    fix15 m20,m21,m22;
  };

  fix15 data[9];
} hmat2d_t;

hvec2d_t vec_zero();

hvec2d_t make_vec(fix15 x, fix15 y);
hvec2d_t make_vec_int(int x, int y);

hvec2d_t make_point(fix15 x, fix15 y);
hvec2d_t make_point_int(int x, int y);

fix15 fast_dist(fix15 dx, fix15 dy);

fix15 vec_dot(hvec2d_t a, hvec2d_t b);

// ||a x b|| assuming a.w, b.w == 0
fix15 vec_perp_dot(hvec2d_t a, hvec2d_t b);

fix15 vec_mag(hvec2d_t a);

fix15 vec_angle(hvec2d_t a);

// Smallest angle between two vectors in [-pi, pi]
fix15 vec_vec_angle(hvec2d_t a, hvec2d_t b);

hvec2d_t vec_lerp(fix15 alpha, hvec2d_t a, hvec2d_t b);


// Out-of-place operations
hvec2d_t vec_scale(hvec2d_t a, fix15 scalar);
hvec2d_t vec_div(hvec2d_t a, fix15 scalar);
hvec2d_t vec_normalize(hvec2d_t a);
hvec2d_t vec_homogenize(hvec2d_t a);
hvec2d_t vec_add(hvec2d_t a, hvec2d_t b);
hvec2d_t vec_sub(hvec2d_t a, hvec2d_t b);
hvec2d_t vec_rotate(hvec2d_t vec, fix15 angle);
hvec2d_t vec_apply_matrix( hvec2d_t v, hmat2d_t m);

// In-place operations
void vec_scale_in(hvec2d_t* a, fix15 scalar);
void vec_div_in(hvec2d_t* a, fix15 scalar);
void vec_normalize_in(hvec2d_t* a);
void vec_homogenize_in(hvec2d_t* a);
void vec_add_in(hvec2d_t* a, hvec2d_t b);
void vec_sub_in(hvec2d_t* a, hvec2d_t b);
void vec_rotate_in(hvec2d_t* vec, fix15 angle);
void vec_apply_matrix_in(hvec2d_t* v, hmat2d_t m);

hvec2d_t vec_cross(hvec2d_t a, hvec2d_t b);
hmat2d_t vec_outer(hvec2d_t a, hvec2d_t b);

hmat2d_t mat_prod(hmat2d_t a, hmat2d_t b);


#endif  //!__LINALG__H__