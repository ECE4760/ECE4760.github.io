/** snake_state.c
 * Implementation of Snake state tracking.
 */

#include "snake_state.h"
#include "smath/fix.h"
#include "smath/linalg.h"
#include <pico/platform/compiler.h>
#include <pico/rand.h>
#include <stdint.h>

// For common init function
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"

#define CLAMP(val, min, max) (MAX((min), (MIN((val), (max)))))

#define SYS_CLOCK_KHZ 270000

snake_t snakes[MAX_SNAKES];
pellet_t pellets[MAX_PELLETS];
// snake_game_t gs;

// Coordinate system definition
// Any point on the board can be expressed as origin + x * xhat + y * yhat
hvec2d_t origin = {0, 0, 1};
const hvec2d_t xhat = {ONE15, 0, 0};
const hvec2d_t yhat = {0, ONE15, 0};

const fix15 segment_len = int2fix15(SEGMENT_LEN);

fix15 snake_board_left, snake_board_right, snake_board_top, snake_board_bottom;

uint16_t spawn_min_x, spawn_max_x;
uint16_t spawn_min_y, spawn_max_y;

fix15 turn_accumulator[MAX_SNAKES] = {0};
fix15 current_pellet_id[MAX_SNAKES] = {0};
fix15 avoid_pellet_id[MAX_SNAKES] = {0};
fix15 avoid_pellet_counter[MAX_SNAKES] = {0};
fix15 same_pellet_accum[MAX_SNAKES] = {0};

/**
 * Compute the center of mass of the set of body segments between the `start`
 * and `end` indices (both inclusive). The mass of the segments will be saved to
 * `mass` if `mass != NULL`.
 *
 * Computed based on order of arguments:
 * - ( start <  end   ) => start is weighted highest
 * - ( end   <  start ) => end is weighted highest
 * - ( start == -1    ) => head_pos is the start point
 * - ( end   == -1    ) => head_pos is the end point
 * - ( start == end   ) => returns `(hvec2d_t)snake->body[start]`
 */
hvec2d_t snake_center_of_mass(snake_t *snake, int start, int end, fix15 *mass) {
  // NOTE: There's a lot of potential for overflows here if the weights are too
  // high

  hvec2d_t *segs = (hvec2d_t *)&snake->body;

  if (start == end) {
    return segs[start];
  }

  unsigned int n = abs(start - end) + 1;

  // Mass is linear sequence so it's simple to calculate it's total:
  // n elements with a starting mass of 0 and ending mass of n (or vice versa)
  // => Sn = (n/2)*(0 + n) == (n^2) / 2, leaving off the division by two until
  // the end
  uint32_t total_mass2 = n * n;

  // Current point's mass
  fix15 point_mass;
  // Mass to add or subtract from
  fix15 starting_mass;
  fix15 iter;

  uint16_t first;
  uint16_t last;

  // Center of mass accumulator
  hvec2d_t com = vec_zero();

  if (start < end) {
    // Start with mass of n, subtract 1 each time
    starting_mass = int2fix15(n);
    iter = -ONE15;

    // start -> end
    last = end;

    if (start == -1) {
      // Add head * n to center of mass,
      com = vec_scale(snake->head_pos, starting_mass);
      // Starting mass for the rest is one less
      starting_mass -= ONE15;
      first = 0;
    } else {
      first = start;
    }
  } else {
    // Start with mass of 0, add 1 each time
    starting_mass = 0;
    iter = ONE15;

    // end -> start
    last = start;

    if (end == -1) {
      // Add head * 0 to center of mass (nop)
      // Starting mass for the rest is one more
      starting_mass += ONE15;
      first = 0;
    } else {
      first = end;
    }
  }

  point_mass = starting_mass;

  for (int i = first; i <= last; i++) {
    // Multiply point by its mass and add to the accumulator
    vec_add_in(&com, vec_scale(segs[i], point_mass));

    // Add or subtract as necessary for next point
    point_mass += iter;
  }

  // Divide the sum by the total to get the center of mass
  fix15 scale = divfix15((ONE15 << 1), int2fix15(total_mass2)); // 2/n^2
  vec_scale_in(&com, scale);

  if (mass != NULL) {
    // Divide by 2 then shift, done in one step here to keep a bit more
    // precision
    *mass = (fix15)(total_mass2 << 14);
  }

  return com;
}

void snake_add_segment(snake_t *snake);

hvec2d_t rand_bounded_point(fix15 min_x, fix15 min_y, fix15 max_x,
                            fix15 max_y) {
  return (hvec2d_t){rand_fix15(min_x, max_x), rand_fix15(min_y, max_y), ONE15};
}

hvec2d_t rand_spawn_point() {
  uint32_t x = (get_rand_32() % (spawn_max_x - spawn_min_x)) + spawn_min_x;
  uint32_t y = (get_rand_32() % (spawn_max_y - spawn_min_y)) + spawn_min_y;

  return (hvec2d_t){int2fix15(x), int2fix15(y), ONE15};
}

hvec2d_t find_velocity_vec(fix15 speed, fix15 heading) {
  return (hvec2d_t){multfix15(speed, cosfx15(heading)),
                    multfix15(speed, sinfx15(heading)), 0};
}

// Initialize the game-state with given board parameters
void snake_game_init(int board_left, int board_top, int board_right,
                     int board_bottom) {
  snake_board_left = int2fix15(board_left);
  snake_board_top = int2fix15(board_top);
  snake_board_right = int2fix15(board_right);
  snake_board_bottom = int2fix15(board_bottom);

  origin.x = snake_board_left;
  origin.y = snake_board_top;
  origin.w = ONE15;

  spawn_min_x = board_left + (HEAD_RADIUS * 3);
  spawn_max_x = board_right - (HEAD_RADIUS * 3);
  spawn_min_y = board_top + (HEAD_RADIUS * 3);
  spawn_max_y = board_bottom - (HEAD_RADIUS * 3);

  // gs->active_snakes = 0;
  // gs->active_pellets = 0;

  memset(&snakes, 0, sizeof(snakes));
  memset(&pellets, 0, sizeof(pellets));

  for (int i = 0; i < MAX_SNAKES; i++) {
    snakes[i].id = i + 1;
  }
}

snake_t *snake_get(int id) { return &snakes[id - 1]; }

fix15 snake_get_world_angle(snake_t *snake) {
  return vec_vec_angle(xhat, snake->heading);
}

fix15 snake_get_goal_world_angle(snake_t *snake) {
  return vec_vec_angle(xhat, snake->goal_heading);
}

// Spawn one `snake` at (x,y) `position` `angle` rads from x-axis
void snake_spawn(snake_t *snake, hvec2d_t position, fix15 angle) {
  snake->head_pos = position;
  snake->heading = vec_rotate(xhat, angle);
  snake->goal_heading = snake->heading;
  snake->num_segments = 0;
  snake->speed = SNAKE_SPEED;
  memset(snake->body, 0, sizeof(snake->body));
  snake->score = 1;
#if (SNAKE_SCORE_METHOD == SNAKE_FIB)
  snake->next_seg_score = 1;
  snake->last_seg_score = 0;
#endif
  snake_add_segment(snake);
}

// Spawn one snake at a random (valid) position
hvec2d_t snake_spawn_rand(snake_t *snake) {
  fix15 angle = rand_fix15_2pi();
  snake->head_pos = rand_spawn_point();
  snake->heading = vec_rotate(xhat, angle);
  snake->goal_heading = snake->heading;
  snake->speed = SNAKE_SPEED;
  snake->num_segments = 0;
  memset(snake->body, 0, sizeof(snake->body));
  snake->score = 1;
#if (SNAKE_SCORE_METHOD == SNAKE_FIB)
  snake->next_seg_score = 1;
  snake->last_seg_score = 0;
#endif
  snake_add_segment(snake);

  return snake->head_pos;
}

// Spawn all snakes randomly
void snake_spawn_all() {
  for (int i = 0; i < MAX_SNAKES; i++) {
    snake_spawn_rand(&snakes[i]);
  }
}

void snake_game_start(int num_snakes, int num_pellets) {
  snake_spawn_all();

  snake_spawn_all_pellets();
}

pellet_t *snake_find_closest_pellet(snake_t *snake, hvec2d_t *disp,
                                    fix15 *dist) {
  pellet_t *closest;
  hvec2d_t closest_disp;
  fix15 closest_dist = FIX15_MAX;

  for (int p = 0; p < MAX_PELLETS; p++) {
    pellet_t *plt = &pellets[p];
    hvec2d_t disp_ = vec_sub(plt->position, snake->head_pos);

    fix15 dist = vec_mag(disp_);
    if (dist < closest_dist) {
      closest = plt;
      closest_disp = disp_;
      closest_dist = dist;
    }
  }

  if (disp != NULL) {
    *disp = closest_disp;
  }

  if (dist != NULL) {
    *dist = closest_dist;
  }

  return closest;
}


pellet_t *snake_find_weighted_pellet(snake_t *snake, hvec2d_t *disp,
                                    fix15 *dist) {

  pellet_t *best_pellet = NULL;
  hvec2d_t best_disp = {0, 0, 0};
  fix15 best_dist = 0;
  fix15 best_score = FIX15_MAX;  // Lower score = better pellet
  fix15 my_score = FIX15_MAX;

  // Evaluate each pellet
  for (int p = 0; p < MAX_PELLETS; p++) {
    pellet_t *plt = &pellets[p];
    hvec2d_t disp_to_pellet = vec_sub(plt->position, snake->head_pos);
    fix15 my_dist = vec_mag(disp_to_pellet);
    
    if (my_dist > best_score) continue; //Omit if too far away
    if (avoid_pellet_id[snake->id - 1] == plt->id) {
      if (avoid_pellet_counter[snake->id - 1] > 0) {
        avoid_pellet_counter[snake->id - 1]--;
        continue; // skip
      } else {
        avoid_pellet_id[snake->id - 1] = 0; // Clear when counter expires
      }
    }
    
    // Calculate base score from distance (closer = lower score = better)
    my_score = my_dist;
    
    // Add penalty for other snakes being close to this pellet
    for (int s = 0; s < MAX_SNAKES; s++) {
      snake_t *other_snake = &snakes[s];
      if (other_snake == snake) continue;
      
      hvec2d_t other_snake_disp = vec_sub(plt->position, other_snake->head_pos);
      fix15 other_dist = vec_mag(other_snake_disp);
      
      // If other snake is closer to this pellet, heavily penalize it
      if (other_dist < my_dist) {
        fix15 penalty = multfix15(my_dist - other_dist, int2fix15(3)); // 3x penalty
        my_score += penalty;
      }
      
      // Add smaller penalty for other snakes being nearby
      if (other_dist < int2fix15(60)) {
        fix15 proximity_penalty = divfix15(int2fix15(30), other_dist + ONE15);
        my_score += proximity_penalty;
      }
      
      
      hvec2d_t other_heading_to_pellet = vec_normalize(other_snake_disp);
      fix15 heading_alignment = vec_dot(other_snake->goal_heading, other_heading_to_pellet);
      // Check if other snake is moving toward this pellet
      if (heading_alignment > float2fix15(0.7f) && other_dist < int2fix15(80)) { 
        fix15 competition_penalty = multfix15(heading_alignment, int2fix15(20));
        my_score += competition_penalty; // If so, add penalty
      }
    }
    
    // Update best pellet if this one has a better (lower) score
    if (my_score < best_score) {
      best_score = my_score;
      best_pellet = plt;
      best_disp = disp_to_pellet;
      best_dist = my_dist;
    }
  }

  // Fallback to closest pellet if no good weighted option found
  if (best_pellet == NULL) {
    return snake_find_closest_pellet(snake, disp, dist);
  }

  if (disp != NULL) {
    *disp = best_disp;
  }

  if (dist != NULL) {
    *dist = best_dist;
  }
  
  if (current_pellet_id[snake->id - 1] == best_pellet->id) {
    same_pellet_accum[snake->id - 1] += 1;
    // If stuck on same pellet for too long, reset to closest pellet
    if (same_pellet_accum[snake->id - 1] >= 499) {
      avoid_pellet_id[snake->id - 1] = best_pellet->id;
      avoid_pellet_counter[snake->id - 1] = 150; // Avoid for 30 frames
    }
  } else {
  current_pellet_id[snake->id - 1] = best_pellet->id;
  same_pellet_accum[snake->id - 1] = 0;
  }

  return best_pellet;
}

snake_t *snake_find_closest_snake(snake_t *snake, hvec2d_t *disp, fix15 *dist) {
  snake_t *closest;
  hvec2d_t closest_disp;
  fix15 closest_dist = FIX15_MAX;

  for (int p = 0; p < MAX_SNAKES; p++) {
    snake_t *snk = &snakes[p];

    if (snake == snk)
      continue;

    hvec2d_t disp_ = vec_sub(snk->head_pos, snake->head_pos);

    fix15 dist_ = vec_mag(disp_);
    if (dist_ < closest_dist) {
      closest = snk;
      closest_disp = disp_;
      closest_dist = dist_;
    }

    for (int b = 0; b < snk->num_segments; b++) {
      disp_ = vec_sub(snk->body[b].start_pos, snake->head_pos);

      dist_ = vec_mag(disp_);
      if (dist_ < closest_dist) {
        closest = snk;
        closest_disp = disp_;
        closest_dist = dist_;
      }
    }
  }

  if (disp != NULL) {
    *disp = closest_disp;
  }

  if (dist != NULL) {
    *dist = closest_dist;
  }

  return closest;
}

void snake_change_angle(snake_t *snake, fix15 world_angle) {
  snake->goal_heading = vec_rotate(xhat, world_angle);
}

void snake_target(snake_t *snake, hvec2d_t pt_o_vec) {
  if (pt_o_vec.w == 0) {
    snake->goal_heading = pt_o_vec;
  } else {
    snake->goal_heading = vec_normalize(vec_sub(pt_o_vec, snake->head_pos));
  }
}

void snake_spawn_all_pellets() {
  for (int i = 0; i < MAX_PELLETS; i++) {
    pellets[i].id = i + 1;
    // TODO: Set random value
    snake_spawn_pellet(&pellets[i], rand_spawn_point(), 1);
  }
}

void snake_spawn_pellet(pellet_t *pellet, hvec2d_t position, int8_t value) {
  pellet->position = position;
  pellet->value = value;
}

// Update snake's intended heading
void snake_turn(snake_t *snake, fix15 angle_offset) {
  snake->goal_heading = vec_rotate(snake->heading, angle_offset);
}

void snake_add_segment(snake_t *snake) {
  uint16_t s;
#if (SNAKE_SCORE_METHOD == SNAKE_LOG2)
  if (snake->score == 0)
    return;

  uint16_t lg = 1 + log2i(snake->score);
  s = snake->num_segments;

  if (lg < s)
    return;

  snake->num_segments = lg;
#elif (SNAKE_SCORE_METHOD == SNAKE_FIB)
  if (snake->score < snake->next_seg_score)
    return;

  uint32_t old_next = snake->next_seg_score;
  snake->next_seg_score = snake->next_seg_score + snake->last_seg_score;
  snake->last_seg_score = old_next;

  snake->num_segments++;
#endif

  s = snake->num_segments;
  if (s >= MAX_SNAKE_SEGMENTS)
    return;

  segment_t *to_add = &snake->body[s];
  hvec2d_t to_new;
  // Add segment behind head

  if (snake->num_segments == 0) {
    to_new = vec_scale(snake->heading, -segment_len);
    to_add->start_pos = snake->head_pos;
    to_add->end_offset = to_new;
  } else {
    // New segment starts at the previous's endpoint and ends in the same
    // direction
    to_new = (to_add - 1)->end_offset;
    to_add->start_pos = vec_add((to_add - 1)->start_pos, to_new);
    to_add->end_offset = (to_add - 1)->end_offset;
  }

#if (SNAKE_SCORE_METHOD == SNAKE_ONE)
  snake->num_segments++;
#endif
}

void snake_scale_velocity(snake_t *snake, fix15 scalar) {
  snake->speed = multfix15(snake->speed, scalar);
}

// Check for collisions with another snake or wall and update appropriately.
// returns -1 for wall collisions, 0 for no collision, and snake ID for a
// snake collision
int snake_check_collision(snake_t *snake) {
  // Check walls
  if (snake->head_pos.x - int2fix15(HEAD_RADIUS) <= snake_board_left ||
      snake->head_pos.x + int2fix15(HEAD_RADIUS) >= snake_board_right ||
      snake->head_pos.y + int2fix15(HEAD_RADIUS) >= snake_board_bottom ||
      snake->head_pos.y - int2fix15(HEAD_RADIUS) <= snake_board_top) {
    snake_spawn_rand(snake);
    return -1;
  }

  for (int i = 0; i < MAX_SNAKES; i++) {
    snake_t *to_check = &snakes[i];

    if (to_check->id == snake->id) {
      continue;
    }
    // TODO: Update for vectors
    fix15 dx;
    fix15 dy;
    fix15 dist;
    for (int i = 0; i < to_check->num_segments; i++) {
      dx = to_check->body[i].start_pos.x - snake->head_pos.x;
      dy = to_check->body[i].start_pos.y - snake->head_pos.y;
      dist = fast_dist(dx, dy);

      if (dist < int2fix15(HEAD_RADIUS)) {
        snake_spawn_rand(snake);
        return to_check->id;
      }
    }

    dx = to_check->head_pos.x - snake->head_pos.x;
    dy = to_check->head_pos.y - snake->head_pos.y;
    dist = fast_dist(dx, dy);
    if (dist < int2fix15(HEAD_RADIUS * 2)) {
      snake_spawn_rand(snake);
      snake_spawn_rand(to_check);
      return to_check->id;
    }
  }

  return 0;
}

// Check for collision with food pellet and update data.
int snake_check_pellets(snake_t *snake) {
  for (int i = 0; i < MAX_PELLETS; i++) {
    fix15 dx = pellets[i].position.x - snake->head_pos.x;
    fix15 dy = pellets[i].position.y - snake->head_pos.y;
    fix15 dist = fast_dist(dx, dy);

    if (dist <= int2fix15((HEAD_RADIUS + PELLET_RADIUS))) {
      for (int i = 0; i < pellets[i].value; i++) {
        snake->score++;
        snake->speed--; // Slightly reduce speed
        snake_add_segment(snake);
      }
      pellets[i].position = rand_spawn_point();

      return i;
    }
  }

  return -1;
}


// Update the snake's heading and position based on time-step `delta_time`
void snake_update_position(snake_t *snake, fix15 delta_time) {
  fix15 angle_diff = vec_vec_angle(snake->heading, snake->goal_heading);
  fix15 angle_delta = multfix15(TURN_SPEED, delta_time);

  
  // Flip the sign of angular displacement if needed
  if (angle_diff < 0)
    angle_delta = -angle_delta;

  // Clamp to angle difference (no overshoot)
  fix15 clamped_diff = (abs(angle_diff) > abs(angle_delta)) ? angle_delta : angle_diff;
  // |angle_diff| <= |angle_delta| 

  // turn_accumulator[snake->id - 1] += (clamped_diff >> 5) - (turn_accumulator[snake->id - 1] >> 3);
  // clamped_diff = ;
  

  snake->heading = vec_rotate(snake->heading, clamped_diff);

  // fix15 speed_change = multfix15(ONE15 >> 6, delta_time) - abs(clamped_diff);
  // if (speed_change > 0) {
  //   speed_change = multfix15(speed_change, PTZEROONE15 - 20);
  // } else {
  //   speed_change = multfix15(speed_change, (PTZEROONE15 + 20));
  // }

  // snake->speed += speed_change;
  // snake->speed = CLAMP(snake->speed, SNAKE_SPEED_MIN,
  //                      SNAKE_SPEED_MAX - snake->num_segments);



  fix15 disp_mag = multfix15(snake->speed, delta_time);
  hvec2d_t disp_vec = vec_scale(snake->heading, disp_mag);

  // hvec2d_t old_pos = snake->head_pos;
  snake->head_pos = vec_add(snake->head_pos, disp_vec);

  if (snake->num_segments > 0) {
    segment_t *seg;
    hvec2d_t curr_pos;
    hvec2d_t old_end;
    hvec2d_t pt_disp;
    fix15 pt_disp_mag;
    fix15 proj_scale;

    curr_pos = snake->head_pos;
    for (int i = 0; i < snake->num_segments - 1; i++) {
      seg = &snake->body[i];

      old_end = vec_add(seg->start_pos, seg->end_offset);

      // Translate the start position
      seg->start_pos = curr_pos;

      // Find the displacement vector from the new start to next point
      pt_disp = vec_sub(old_end, seg->start_pos);
      // Rotate the current vector to match the displacement vector (i.e.
      // project onto it while preserving length)
      angle_diff = vec_vec_angle(seg->end_offset, pt_disp);
      vec_rotate_in(&seg->end_offset, angle_diff);

      // Update the current postion for the next segment
      curr_pos = vec_add(seg->start_pos, seg->end_offset);
    }

    // No point after to reference, so we have to do slightly more work for the
    // last segment
    seg = &snake->body[snake->num_segments - 1];

    old_end = vec_add(seg->start_pos, seg->end_offset);

    pt_disp = vec_sub(old_end, curr_pos);

    seg->start_pos = curr_pos;

    // Rotate the current vector to match the displacement vector (i.e.
    // project onto it while preserving length)
    angle_diff = vec_vec_angle(seg->end_offset, pt_disp);
    vec_rotate_in(&seg->end_offset, angle_diff);
    // Last point at seg->start_pos + seg->end_offset
  }
}

/**
 * Common hardware and system initialization for all build configurations.
 * Call this function at the beginning of main() before any other setup.
 * 
 * This function handles:
 * - Voltage regulation setup
 * - System clock configuration 
 * - Standard I/O initialization
 */
void snake_common_init(void) {
   // set the clock
  vreg_set_voltage(VREG_VOLTAGE_MAX);
  set_sys_clock_khz(SYS_CLK_KHZ, true);
  
  // Initialize standard I/O (USB serial, UART, etc.)
  stdio_init_all();
}


// Distance from point p to segment [a, b]
// Can handle any length of segment
fix15 distance_point_to_segment_fix15(hvec2d_t p,
                                             hvec2d_t a,
                                             hvec2d_t b,
                                             hvec2d_t *dir_away)
{
    hvec2d_t ab = vec_sub(b, a);
    fix15 dist_ = vec_mag(ab);
    fix15 ab2 = multfix15(dist_, dist_);  // ab squared

    // t in [0,1] along segment in fix15
    hvec2d_t pa = vec_sub(p, a);
    fix15 num   = vec_dot(pa, ab);
    fix15 t     = divfix15(num, ab2);   // dimensionless

    // Clamp t to [0, 1]
    if (t < 0)      t = 0;
    if (t > ONE15)  t = ONE15;

    // closest = a + ab * t
    hvec2d_t ab_t   = vec_scale(ab, t);
    hvec2d_t closest = vec_add(a, ab_t);

    // away = p - closest
    *dir_away = vec_sub(p, closest);
    fix15 d = vec_mag(*dir_away);

    return d;
}


// ===== Tunable parameters (fix15) =====
fix15 R_SEG_INFLUENCE;    // px
fix15 R_WALL_INFLUENCE;    // px

fix15 SEG_D_SCALE;
fix15 WALL_D_SCALE;

fix15 K_FOOD;           
fix15 K_SEG_REP;
fix15 K_WALL_REP;

fix15 STRENGTH_MAX_SEG;
fix15 STRENGTH_MAX_WALL;
fix15 MAX_REP_MAG;

fix15 EPS_MAG;      // small magnitude threshold



//deprecated, now just a helper function to highlight the closest snake segment to the current head
void old_ai(snake_t *snk, 
    hvec2d_t *closest_snake_to_head_displacement, 
    bool is_selected){
  hvec2d_t cs_disp;
  fix15 sclosest_dist;
  snake_t *closest_snake;
  if (is_selected){
    closest_snake =
      snake_find_closest_snake(snk, &cs_disp, &sclosest_dist);
    *closest_snake_to_head_displacement = cs_disp;
  }
}

void snake_wall_repulsion(fix15 dist_wall, hvec2d_t normal, fix15 offset, hvec2d_t *repulse){
  if (dist_wall < R_WALL_INFLUENCE) {
    dist_wall = multfix15(dist_wall, WALL_D_SCALE);
    fix15 d_plus = dist_wall + ONE15;
    if (d_plus <= 0) d_plus = ONE15;

    fix15 inv_d  = divfix15(ONE15, d_plus);
    fix15 inv_d2 = multfix15(inv_d, inv_d);

    fix15 base = inv_d2 - offset;
    if (base < 0) base = 0;

    fix15 strength = multfix15(K_WALL_REP, base);
    if (strength > STRENGTH_MAX_WALL)
        strength = STRENGTH_MAX_WALL;

    hvec2d_t contrib = vec_normalize(normal);
    contrib = vec_scale(contrib, strength);
    vec_add_in(repulse, contrib);
  }
}

void snake_ai_choose_goal_fix15(snake_t *me, pellet_t **closest_pellet_to_head,
hvec2d_t *closest_pellet_to_head_displacement, bool is_selected)
{
    // ===== Tunable parameters (fix15) =====
    R_SEG_INFLUENCE   = int2fix15(80);    // px 
    R_WALL_INFLUENCE  = int2fix15(20);    // px 

    SEG_D_SCALE       = float2fix15(2.0f);
    WALL_D_SCALE      = float2fix15(0.75f);

    K_FOOD            = float2fix15(0.2f);            // Much weaker food attraction
    K_SEG_REP         = float2fix15(50.0f);
    //K_WALL_REP        = float2fix15(300.0f);
    K_WALL_REP = multfix15(K_SEG_REP, int2fix15(2));

    STRENGTH_MAX_SEG  = float2fix15(80.f);              
    //STRENGTH_MAX_WALL = float2fix15(40.f);
    STRENGTH_MAX_WALL = multfix15(STRENGTH_MAX_SEG, int2fix15(2));
    MAX_REP_MAG       = float2fix15(400.0f);         
    EPS_MAG           = PTZEROONE15;      // small magnitude threshold

    // Precompute 1/(R+1)^2 constants for segments/walls
    const fix15 Rseg_plus  = R_SEG_INFLUENCE  + ONE15;
    const fix15 Rwall_plus = R_WALL_INFLUENCE + ONE15;

    const fix15 inv_Rseg   = divfix15(ONE15, Rseg_plus);
    const fix15 inv_Rseg2  = multfix15(inv_Rseg, inv_Rseg);

    const fix15 inv_Rwall  = divfix15(ONE15, Rwall_plus);
    const fix15 inv_Rwall2 = multfix15(inv_Rwall, inv_Rwall);

    // head position 
    hvec2d_t head = me->head_pos;

    // Food attraction 
    hvec2d_t attract = vec_zero();
    fix15 dist;
    hvec2d_t pellet_closest_disp;
    pellet_t *pellet_closest = snake_find_weighted_pellet(me, &pellet_closest_disp, &dist);
    //pellet_t *pellet_closest = snake_find_closest_pellet(me, &pellet_closest_disp, &dist);

    if (pellet_closest != NULL && dist > EPS_MAG) {
        // disp is displacement from head to pellet (in world coords)
        hvec2d_t dir = vec_normalize(pellet_closest_disp);      // normalized
        attract = vec_scale(dir, K_FOOD);        // K_FOOD * dir
    }

    if (is_selected){
      *closest_pellet_to_head = pellet_closest;
      *closest_pellet_to_head_displacement = pellet_closest_disp;
    }

    // Repulsion from snake segments AND heads 
    hvec2d_t total_repulse = vec_zero();

    for (int s = 0; s < MAX_SNAKES; s++) {
        snake_t *sn = &snakes[s];
        if(sn != me){
          hvec2d_t head_disp = vec_sub(head, sn->head_pos);
          fix15 head_dist = vec_mag(head_disp);
          
          if (head_dist < R_SEG_INFLUENCE && head_dist > EPS_MAG) {
              hvec2d_t head_dir = vec_normalize(head_disp);
              
              fix15 scaled_dist = multfix15(head_dist, SEG_D_SCALE);
              fix15 d_plus = scaled_dist + ONE15;
              if (d_plus <= 0) d_plus = ONE15;
              
              fix15 inv_d = divfix15(ONE15, d_plus);
              fix15 inv_d2 = multfix15(inv_d, inv_d);
              
              fix15 base = inv_d2 - inv_Rseg2;
              if (base < 0) base = 0;
              
              // Useing much stronger repulsion for heads to prevent collisions
              fix15 head_strength = multfix15(multfix15(K_SEG_REP, int2fix15(4)), base);
              if (head_strength > multfix15(STRENGTH_MAX_SEG, int2fix15(4)))
                  head_strength = multfix15(STRENGTH_MAX_SEG, int2fix15(4));
              
              hvec2d_t head_contrib = vec_scale(head_dir, head_strength);
              vec_add_in(&total_repulse, head_contrib);
          }
          
          // repulsion from segments
          for (uint16_t i = 0; i < sn->num_segments; i++) {
              segment_t *seg = &sn->body[i];

              // Segment endpoints in world coords
              hvec2d_t dir_away;
              fix15 d = distance_point_to_segment_fix15(head, seg->start_pos, 
                vec_add(seg->start_pos, seg->end_offset), &dir_away);

              if (d > R_SEG_INFLUENCE)
                  continue;

              d = multfix15(d, SEG_D_SCALE);
              // inv_d2 = 1/(d+1)^2
              //fix15 d_plus = d + ONE15;
              //if (d_plus <= 0) d_plus = ONE15; // guard

              fix15 inv_d    = divfix15(ONE15, d);
              fix15 inv_d2   = multfix15(inv_d, inv_d);

              // base = inv_d2 - inv_Rseg2, clamped to >= 0
              fix15 base = inv_d2 - inv_Rseg2;
              if (base < 0) base = 0;

              fix15 strength = multfix15(K_SEG_REP, base);

              if (strength > STRENGTH_MAX_SEG)
                  strength = STRENGTH_MAX_SEG;
              //clamp contribution from this segment
              

              // repulse += dir_away * strength
              hvec2d_t contrib = vec_scale(dir_away, strength);
              vec_add_in(&total_repulse, contrib);
          }
        }
    }

    // ===== Repulsion from walls =====
    // Left wall: normal = (1, 0)
    fix15 d_left = head.x - snake_board_left;
    snake_wall_repulsion(d_left, make_vec(ONE15, 0), inv_Rwall2, &total_repulse);

    // Right wall: normal = (-1, 0)
    fix15 d_right = snake_board_right - head.x;
    snake_wall_repulsion(d_right, make_vec(-ONE15, 0), inv_Rwall2, &total_repulse);

    // Top wall: normal = (0, 1)
    fix15 d_top = head.y - snake_board_top;
    snake_wall_repulsion(d_top, make_vec(0, ONE15), inv_Rwall2, &total_repulse);

    // Bottom wall: normal = (0, -1)
    fix15 d_bottom = snake_board_bottom - head.y;
    snake_wall_repulsion(d_bottom, make_vec(0, -ONE15), inv_Rwall2, &total_repulse);

    // Clamp total repulsion magnitude 
    if (vec_mag(total_repulse) > MAX_REP_MAG) {
        vec_normalize_in(&total_repulse);  // normalize
        vec_scale_in(&total_repulse, MAX_REP_MAG); //clamp
    }

    // Combine attraction + repulsion 
    // Emergency avoidance: if repulsion is very strong, ignore food completely
    hvec2d_t desired = vec_mag(total_repulse) > float2fix15(50.0f) ? total_repulse : vec_add(attract, total_repulse);

    // Fallback if tiny vector: keep current heading
    if (vec_mag(desired) < EPS_MAG) {
        desired = me->heading;
    }
   
    //vec_scale_in(&desired, float2fix15(0.8f));
    //vec_rotate_in(&desired, rand_fix15_11() >> 4); // add some noise

    vec_normalize_in(&desired);  // ensure unit length
    snake_target(me, desired);
}