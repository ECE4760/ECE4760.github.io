/** snake_state.h
 * Distributed snake game-state definitions.
 */

#ifndef __SNAKE_STATE__H__
#define __SNAKE_STATE__H__

#include "smath/fix.h"
#include "smath/linalg.h"
#include "config/game.h"
#include <assert.h>
#include <stdint.h>

// Common parameters
// Distances are in pixels
// Angles are in radians
// Velocities are px/ms

#ifndef PELLET_RADIUS
#define PELLET_RADIUS 3
#endif

#ifndef HEAD_RADIUS
#define HEAD_RADIUS 5
#endif

#ifndef MAX_SNAKE_SEGMENTS
#define MAX_SNAKE_SEGMENTS 100
#endif

#ifndef MAX_SNAKES
#define MAX_SNAKES 1
#endif

#ifndef MAX_PELLETS
#define MAX_PELLETS 50
#endif

#ifndef SNAKE_SPEED
#define SNAKE_SPEED 100
#endif

#ifndef TURN_SPEED
#define TURN_SPEED 5
#endif


#if defined (static_assert)
#define STR(s) #s
#define XSTR(s) STR(s)

// Static assert with the arguments and print message with macro name and values
#define CHECK_LIMIT(d, lim) static_assert((d) <= (lim), #d " exceeds limit (" XSTR(d) " > " XSTR(lim) ")")

CHECK_LIMIT(MAX_SNAKES, 255);
CHECK_LIMIT(MAX_SNAKE_SEGMENTS, 65535);
CHECK_LIMIT(MAX_PELLETS, 65535);

#endif

// Forward declarations
// typedef uint8_t;
// typedef uint16_t;
// typedef uint32_t;
// typedef fix15;


// Food pellet
typedef struct pellet {
  uint16_t      id;
  hvec2d_t      position;
  int8_t        value;
} pellet_t;


typedef struct snake_segment {
  hvec2d_t start_pos;
  hvec2d_t end_offset;
} segment_t;

typedef struct snake {
  uint8_t       id;
  uint32_t      score;
#if (SNAKE_SCORE_METHOD == SNAKE_FIB)
  uint32_t      next_seg_score;
  uint32_t      last_seg_score;
#endif
  hvec2d_t      head_pos;
  hvec2d_t      heading;
  hvec2d_t      goal_heading;
  fix15         speed;
  uint16_t      num_segments;
  segment_t     body[MAX_SNAKE_SEGMENTS];
  bool          manually_controlled;
} snake_t;

// // TODO: Do something with this or delete it
// struct snake_game_state {
//   uint8_t       active_snakes;
//   uint16_t      active_pellets;
// };


// typedef struct snake_game_state* snake_game_t;

extern snake_t snakes[MAX_SNAKES];
extern pellet_t pellets[MAX_PELLETS];
//extern snake_game_t gs;


// Initialize the game-state with given board parameters
void snake_game_init(int board_left, int board_top, int board_right, int board_bottom);

// Common hardware and system initialization for all build configurations
void snake_common_init(void);

snake_t* snake_get(int id);

fix15 snake_get_world_angle(snake_t* snake);

fix15 snake_get_goal_world_angle(snake_t* snake);

// Spawn one `snake` at (x,y) `position` `angle` rads from x-axis
void snake_spawn(snake_t* snake, hvec2d_t position, fix15 angle);

// Spawn one snake at a random (valid) position
hvec2d_t snake_spawn_rand(snake_t* snake);

// Spawn all snakes randomly
void snake_spawn_all();

void snake_spawn_all_pellets();

void snake_spawn_pellet(pellet_t* pellet, hvec2d_t position, int8_t value);

void snake_game_start(int num_snakes, int num_pellets);

pellet_t* snake_find_closest_pellet(snake_t* snake, hvec2d_t* disp, fix15* dist);

snake_t* snake_find_closest_snake(snake_t* snake, hvec2d_t* disp, fix15* dist);

// Update snake's intended heading
void snake_change_angle(snake_t* snake, fix15 world_angle);

void snake_turn(snake_t* snake, fix15 angle_offset);

// Set a target heading
// If passing in a point, changes goal heading to a normalized vector to target
// If passing in a vector, changes goal heading to that vector (MUST BE NORMALIZED)
void snake_target(snake_t* snake, hvec2d_t pt_o_vec);

void snake_scale_velocity(snake_t* snake, fix15 scalar);

// Check for collisions with another snake or wall and update appropriately.
int snake_check_collision(snake_t* snake);

// Check for collision with food pellet and update data.
int snake_check_pellets(snake_t* snake);

// Update the snake's heading and position based on time-step `delta_time`
void snake_update_position(snake_t* snake, fix15 delta_time);

void snake_ai_choose_goal_fix15(
    snake_t *me, pellet_t **closest_pellet_to_head,
hvec2d_t *closest_pellet_to_head_displacement, bool is_selected);

void old_ai(snake_t *snk,
hvec2d_t *closest_snake_to_head_displacement, bool is_selected);

fix15 distance_point_to_segment_fix15(hvec2d_t p,
                                             hvec2d_t a,
                                             hvec2d_t b,
                                             hvec2d_t *dir_away);

#endif  //!__SNAKE_STATE__H__