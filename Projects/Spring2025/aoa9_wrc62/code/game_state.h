/*Akinfolami Akin - Alamu(aoa9), Wilson Coronel(wrc62) */
#include "pico/divider.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#ifndef GAME_STATE_H
#define GAME_STATE_H

// Game board dimensions
#define ROWS 7
#define COLS 15

// Number of boids (autonomous agents) in the simulation
#define NUM_BOIDS 2

// === Fixed Point Arithmetic Macros
// ======================================== These macros handle fixed-point
// calculations for better performance Using 15-bit fixed point
// representation (1 sign bit + 15 fractional bits)
typedef signed int fix15;
#define multfix15(a, b)                                                        \
  ((fix15)((((signed long long)(a)) * ((signed long long)(b))) >> 15))
#define float2fix15(a) ((fix15)((a) * 32768.0)) // 2^15
#define fix2float15(a) ((float)(a) / 32768.0)
#define absfix15(a) abs(a)
#define int2fix15(a) ((fix15)(a << 15))
#define fix2int15(a) ((int)(a >> 15))
#define char2fix15(a) (fix15)(((fix15)(a)) << 15)
#define divfix(a, b)                                                           \
  (fix15)(div_s64s64((((signed long long)(a)) << 15), ((signed long long)(b))))

// === Boid Simulation Parameters ========================================
// These constants define the behavior of the boids in the simulation
#define VISUAL_RANGE float2fix15(40.0)   // How far a boid can see
#define PROTECTED_RANGE float2fix15(8.0) // Minimum distance between boids
#define CENTERING_FACTOR                                                       \
  float2fix15(0.0005)                 // Tendency to move toward center of mass
#define AVOID_FACTOR float2fix15(0.7) // Tendency to avoid collisions
#define MATCHING_FACTOR                                                        \
  float2fix15(0.005)                  // Tendency to match velocity of neighbors
#define TURN_FACTOR float2fix15(0.02) // How quickly boids can turn
#define MAX_SPEED float2fix15(2.0)    // Maximum speed limit
#define MIN_SPEED float2fix15(1.0)    // Minimum speed limit
#define BIAS_VAL_GROUP1 float2fix15(0.001)  // Initial bias for group 1
#define BIAS_VAL_GROUP2 float2fix15(0.001)  // Initial bias for group 2
#define BIAS_INCREMENT float2fix15(0.00004) // Rate of bias change
#define MAX_BIAS float2fix15(0.01)          // Maximum allowed bias
#define BOID_RADIUS 4                       // Visual radius of boids

// === Screen Layout Constants ========================================
// Defines the layout and boundaries of the game screen
#define LEFT_MARGIN int2fix15(10)
#define RIGHT_MARGIN int2fix15(620) // 640 - 20
#define TOP_MARGIN int2fix15(200)
#define BOTTOM_MARGIN int2fix15(280) // 480 - 100
#define GRID_START_X 10
#define GRID_START_Y 80
#define CELL_WIDTH 40
#define CELL_HEIGHT 40
#define MAX_PIXEL_SHIFT int2fix15(10)

// Maximum number of bad number groups that can exist
#define MAX_BAD_GROUPS (ROWS * COLS)

// Collision detection radius for boids
#define BOID_COLLISION_RADIUS 40

// Scout group configuration
#define NUM_SCOUTS_GROUP1 5
#define NUM_SCOUTS_GROUP2 5

// === Wall Detection Macros ========================================
// Helper macros to detect when boids hit screen boundaries
#define hitBottom(b) (b > BOTTOM_MARGIN)
#define hitTop(b) (b < TOP_MARGIN)
#define hitLeft(a) (a < int2fix15(100))
#define hitRight(a) (a > int2fix15(540))

// === Animation Constants ========================================
#define BOX_ANIM_INCREMENT 5
#define BOX_ANIM_MAX_HEIGHT 50
#define BOX_ANIM_PERCENTAGE_INCREMENT 5

// === Data Structures ========================================

// Represents a box in the game
typedef struct {
  int x;
  int y;
  int width;
  int height;
  int percentage;
} Box;

// Animation states for boxes
typedef enum { ANIM_IDLE, ANIM_GROWING, ANIM_SHRINKING } AnimationState;

// Animated box with emotional state percentages
typedef struct {
  int x;
  int y;
  int width;
  int height;
  int current_anim_height;
  AnimationState anim_state;
  int woe_percentage;    // Percentage of woe emotion
  int frolic_percentage; // Percentage of frolic emotion
  int dread_percentage;  // Percentage of dread emotion
  int malice_percentage; // Percentage of malice emotion
} BoxAnim;

// Progress bar animation state
typedef struct {
  int current_progress;
  int progress_anim_step;
  enum {
    ANIMATION_IDLE,
    ANIMATION_GROWING,
  } anim_state;
} ProgressBarAnimation;

// Represents a boid (autonomous agent) in the simulation
typedef struct {
  fix15 x; // Position
  fix15 y;
  fix15 vx; // Velocity
  fix15 vy;
  fix15 biasval;   // Bias value for group behavior
  int scout_group; // Group affiliation (0: group 1, 1: group 2)
} Boid;

// Represents a bad number in the game
typedef struct {
  int bin_id;
} BadNumber;

// Represents a number cell in the game grid
typedef struct {
  int x;
  int y;
  int number;
  int size;
  int animated_last_frame_by_boid0; // Animation tracking
  int animated_last_frame_by_boid1;
  int refined_last_frame;
  bool is_bad_number;
  BadNumber bad_number;
} Number;

// Represents the game cursor
typedef struct {
  int x;
  int y;
  int width;
  int height;
} Cursor;

// Game state enumeration
typedef enum { START_SCREEN, PLAYING, GAME_WON } PlayState;

// Main game state structure containing all game elements
typedef struct {
  Number state[ROWS][COLS];          // Game grid
  Box boxes[5];                      // Game boxes
  Boid boids[NUM_BOIDS];             // Boids in the simulation
  BoxAnim box_anims[5];              // Animated boxes
  Cursor cursor;                     // Game cursor
  PlayState play_state;              // Current game state
  int total_bad_numbers;             // Count of bad numbers
  ProgressBarAnimation progress_bar; // Progress bar state
} GameState;

// === Function Declarations ========================================
void game_state_init(GameState *state, int seed); // Initialize game state
void game_state_update(GameState *state);         // Update game state
void game_state_draw(GameState *state);           // Draw game state
void game_state_update_boxes(Box *state, int x, int y, int w, int h,
                             int percentage); // Update box states
void spawn_boid(Boid *boid, int group_id);    // Create new boid
void update_boids(GameState *state); // Update boid positions and behaviors
void check_collisions_and_animate(
    GameState *state); // Handle collisions and animations
void animate_numbers(Number *num, fix15 dx, fix15 dy, fix15 shift_x,
                     fix15 shift_y);      // Animate number movements
void group_bad_numbers(GameState *state); // Group bad numbers together
void handle_cursor_refinement(
    GameState *state); // Handle cursor refinement logic

#endif // GAME_STATE_H