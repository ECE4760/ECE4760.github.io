/** linalg.c
 * Implementation of common linear algebra functions.
 */

#include "linalg.h"
#include "fix.h"
#include "pico/float.h"

hvec2d_t vec_zero() { return (hvec2d_t){0, 0, 0}; }

hvec2d_t make_vec(fix15 x, fix15 y) {
  return (hvec2d_t){.x = x, .y = y, .w = 0};
}

hvec2d_t make_vec_int(int x, int y) {
  return make_vec(int2fix15(x), int2fix15(y));
}

hvec2d_t make_point(fix15 x, fix15 y) {
  return (hvec2d_t){.x = x, .y = y, .w = ONE15};
}

hvec2d_t make_point_int(int x, int y) {
  return make_point(int2fix15(x), int2fix15(y));
}

// Alpha max beta min. Fast approximation of sqrt(dx^2 + dy^2)
// See: https://en.wikipedia.org/wiki/Alpha_max_plus_beta_min_algorithm
fix15 fast_dist(fix15 dx, fix15 dy) {
  // Using slightly improved version of the algorithm that splits the domain

  // Parameters
  // alpha0 = 1, beta0 = 0
  const fix15 alpha1 = 29432; // ~= 0.898204193266868
  const fix15 beta1 = 15924; // ~= 0.485968200201465

  // Max and Min
  fix15 abs_a = abs(dx);
  fix15 abs_b = abs(dy);
  fix15 max = MAX(abs_a, abs_b);
  fix15 min = MIN(abs_a, abs_b);

  // z0 and z1
  // z0 = alpha0 * Max + beta0 * Min -> z0 = 1 * Max + 0 => z0 = Max
  // z1 = alpha1 * Max + beta1 * Min -> same as standard version
  fix15 z1 = multfix15(alpha1, max) + multfix15(beta1, min);

  // |z| = max(|z0|, |z1|)
  return MAX(max, z1);
}

fix15 vec_dot(hvec2d_t a, hvec2d_t b) {
  return multfix15(a.x, b.x) + multfix15(a.y, b.y) + multfix15(a.w, b.w);
}

// See https://johnblackburne.blogspot.com/2012/02/perp-dot-product.html for use
fix15 vec_perp_dot(hvec2d_t a, hvec2d_t b) {
  return multfix15(-a.y, b.x) + multfix15(a.x, b.y);
}

// Undefined for non-homogenized vectors
fix15 vec_mag(hvec2d_t a) {
  if (a.w == 0)
    return fast_dist(a.x, a.y);

  return sqrtfix15(vec_dot(a, a));
}

fix15 vec_vec_angle(hvec2d_t a, hvec2d_t b) {
  return atan2fx15(vec_perp_dot(a, b), vec_dot(a, b));
}

// Only defined for 0 <= alpha <= 1
hvec2d_t vec_lerp(fix15 alpha, hvec2d_t a, hvec2d_t b) {

  // if (alpha == 0) {
  //   return a;
  // }
  // if (alpha == 1) {
  //   return b;
  // }

  a = vec_scale(a, (ONE15 - alpha));
  b = vec_scale(b, alpha);

  return vec_add(a, b);
}

fix15 vec_angle(hvec2d_t a) { return atan2fx15(a.y, a.x); }

hvec2d_t vec_homogenize(hvec2d_t a) {
  if (a.w == 0)
    return a;
  a.x = divfix15(a.x, a.w);
  a.y = divfix15(a.y, a.w);
  a.w = ONE15;

  return a;
}

hvec2d_t vec_scale(hvec2d_t v, fix15 c) {
  return (hvec2d_t){multfix15(v.x, c), multfix15(v.y, c), multfix15(v.w, c)};
}

hvec2d_t vec_div(hvec2d_t a, fix15 scalar) {
  return (hvec2d_t){divfix15(a.x, scalar), divfix15(a.y, scalar),
                    divfix15(a.w, scalar)};
}

hvec2d_t vec_normalize(hvec2d_t a) {
  fix15 mag_inv = divfix15(ONE15, vec_mag(a));

  // Scale instead of div to save on expensive divides
  return vec_scale(a, mag_inv);
}

hvec2d_t vec_add(hvec2d_t a, hvec2d_t b) {
  return (hvec2d_t){(a.x + b.x), (a.y + b.y), (a.w + b.w)};
}

hvec2d_t vec_sub(hvec2d_t a, hvec2d_t b) {
  return (hvec2d_t){(a.x - b.x), (a.y - b.y), (a.w - b.w)};
}

hvec2d_t vec_rotate(hvec2d_t vec, fix15 angle) {
  hvec2d_t new_vec = vec;
  fix21 sin21;
  fix21 cos21;
  sincosfx21(fix152fix21(angle), &sin21, &cos21);

  fix15 sin_a = fix212fix15(sin21);
  fix15 cos_a = fix212fix15(cos21);

  new_vec.x = multfix15(vec.x, cos_a) - multfix15(vec.y, sin_a);
  new_vec.y = multfix15(vec.x, sin_a) + multfix15(vec.y, cos_a);

  return new_vec;
}

hvec2d_t vec_apply_matrix(hvec2d_t v, hmat2d_t m) {
  const int num_rows = 3;
  const int num_cols = 3;
  hvec2d_t new_vec;
  for (int row = 0; row < num_rows; row++) {
    fix15 accum = 0;
    for (int col = 0; col < num_cols; col++) {
      accum += multfix15(m.data[(row * num_cols) + col], v.data[col]);
    }
    new_vec.data[row] = accum;
  }

  return new_vec;
}

void vec_scale_in(hvec2d_t *a, fix15 scalar) {
  a->x = multfix15(a->x, scalar);
  a->y = multfix15(a->y, scalar);
  a->w = multfix15(a->w, scalar);
}

void vec_div_in(hvec2d_t *a, fix15 scalar) {
  a->x = divfix15(a->x, scalar);
  a->y = divfix15(a->y, scalar);
  a->w = divfix15(a->w, scalar);
}

void vec_normalize_in(hvec2d_t *a) {
  fix15 mag_inv = divfix15(ONE15, vec_mag(*a));

  // Scale instead of div to save on expensive divides
  vec_scale_in(a, mag_inv);
}

void vec_homogenize_in(hvec2d_t *a) {
  if (a->w == 0)
    return;
  a->x = divfix15(a->x, a->w);
  a->y = divfix15(a->y, a->w);
  a->w = ONE15;
}

void vec_add_in(hvec2d_t *a, hvec2d_t b) {
  a->x += b.x;
  a->y += b.y;
  a->w += b.w;
}

void vec_sub_in(hvec2d_t *a, hvec2d_t b) {
  a->x -= b.x;
  a->y -= b.y;
  a->w -= b.w;
}

// 2D rotation (i.e. 3rd term unaffected)
void vec_rotate_in(hvec2d_t *vec, fix15 angle) {
  fix15 old_x = vec->x;
  fix15 old_y = vec->y;
  fix21 sin21;
  fix21 cos21;
  sincosfx21(fix152fix21(angle), &sin21, &cos21);

  fix15 sin_a = fix212fix15(sin21);
  fix15 cos_a = fix212fix15(cos21);

  vec->x = multfix15(old_x, cos_a) - multfix15(old_y, sin_a);
  vec->y = multfix15(old_x, sin_a) + multfix15(old_y, cos_a);
}

void vec_apply_matrix_in(hvec2d_t *v, hmat2d_t m) {
  const int num_rows = 3;
  const int num_cols = 3;
  hvec2d_t new_vec;
  for (int row = 0; row < num_rows; row++) {
    fix15 accum = 0;
    for (int col = 0; col < num_cols; col++) {
      accum += multfix15(m.data[(row * num_cols) + col], v->data[col]);
    }
    new_vec.data[row] = accum;
  }

  // Overwrite the old vector with the computed product
  *v = new_vec;
}

hmat2d_t vec_outer(hvec2d_t a, hvec2d_t b) {
  const int num_rows = 3;
  const int num_cols = 3;
  hmat2d_t prod;
  for (int row = 0; row < num_rows; row++) {
    for (int col = 0; col < num_cols; col++) {
      prod.data[(row * num_cols + col)] = multfix15(a.data[row], b.data[col]);
    }
  }

  return prod;
}

hvec2d_t vec_cross(hvec2d_t a, hvec2d_t b) {
  // Elementary cross product
  hvec2d_t prod = {
      .x = multfix15(a.y, b.w) - multfix15(a.w, b.y),
      .y = multfix15(a.w, b.x) - multfix15(a.x, b.w),
      .w = multfix15(a.x, b.y) - multfix15(a.y, b.x),
  };

  return prod;
}

hmat2d_t mat_prod(hmat2d_t a, hmat2d_t b) {
  const int num_rows = 3;
  const int num_cols = 3;
  hmat2d_t prod;
  for (int row = 0; row < num_rows; row++) {
    for (int col = 0; col < num_cols; col++) {
      fix15 accum = 0;
      for (int k = 0; col < num_cols; k++) {
        accum += multfix15(a.data[(row * num_cols) + k],
                           b.data[(k * num_cols) + row]);
      }

      prod.data[(row * num_cols) + col] = accum;
    }
  }

  return prod;
}