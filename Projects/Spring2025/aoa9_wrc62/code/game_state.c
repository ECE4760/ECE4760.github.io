/*Akinfolami Akin - Alamu(aoa9), Wilson Coronel(wrc62) */
#include "game_state.h"
#include "vga16_graphics.h"
#include <stdbool.h> // Include for bool type
#include <stdlib.h>
#include <time.h>

// Helper function to check if a position is within valid grid bounds
static inline bool is_valid(int r, int c) {
  return r >= 0 && r < ROWS && c >= 0 && c < COLS;
}

// Initialize the game state with random numbers and setup initial positions
void game_state_init(GameState *state, int seed) {
  // Use a combination of the seed and a fixed value to ensure more randomness
  srand(seed + 0x5D9EA); // Add a fixed value to make the seed more unique

  // Initialize game state counters and progress
  state->total_bad_numbers = 0;
  state->progress_bar.current_progress = 25;
  state->progress_bar.progress_anim_step = 0;

  // Initialize the grid with random numbers and positions
  for (int row = 0; row < ROWS; row++) {
    for (int col = 0; col < COLS; col++) {
      // Generate two random numbers for different properties
      int random_number1 = rand();
      int random_number2 = rand();

      // Set up the number cell properties
      state->state[row][col].number = random_number1 % 10;
      state->state[row][col].x = GRID_START_X + (col * CELL_WIDTH);
      state->state[row][col].y = GRID_START_Y + (row * CELL_HEIGHT);
      state->state[row][col].size = 1;
      state->state[row][col].animated_last_frame_by_boid0 = 0;
      state->state[row][col].animated_last_frame_by_boid1 = 0;

      // Determine if this is a "bad" number (15% chance)
      state->state[row][col].is_bad_number = (random_number1 & 0xF) > 14;
      if (state->state[row][col].is_bad_number) {
        state->total_bad_numbers++;
      }
      state->state[row][col].bad_number.bin_id = random_number2 % 4;
    }
  }

  // Initialize the two boids (scout groups)
  spawn_boid(&state->boids[0], 0);
  spawn_boid(&state->boids[1], 1);

  // Initialize the box animations
  for (int i = 0; i < 5; i++) {
    state->box_anims[i].current_anim_height = 0;
    state->box_anims[i].anim_state = ANIM_IDLE;
    state->box_anims[i].woe_percentage = 40;
    state->box_anims[i].frolic_percentage = 40;
    state->box_anims[i].dread_percentage = 40;
    state->box_anims[i].malice_percentage = 40;
  }

  // Initialize cursor position and size
  state->cursor.x = GRID_START_X;
  state->cursor.y = GRID_START_Y;
  state->cursor.width = CELL_WIDTH;
  state->cursor.height = CELL_HEIGHT;

  // Set initial game state
  state->play_state = START_SCREEN;
}

// Update box properties with new position, size and percentage values
void game_state_update_boxes(Box *state, int x, int y, int w, int h,
                             int percentage) {
  state->x = x;
  state->y = y;
  state->width = w;
  state->height = h;
  state->percentage = percentage;
}

// Placeholder for game state drawing (handled in protothread_graphics)
void game_state_draw(GameState *state) {
  // Drawing is handled in protothread_graphics function
}

// Initialize a boid with starting position and velocity
void spawn_boid(Boid *boid, int group_id) {
  // Start in center of screen
  boid->x = int2fix15(320);
  boid->y = int2fix15(240);

  // Set random initial velocity
  boid->vx = ((rand() & 0xFFFF) * 3) - int2fix15(3);
  boid->vy = ((rand() & 0xFFFF) * 3) - int2fix15(3);

  // Assign scout group and initial bias
  boid->scout_group = group_id;
  if (group_id == 0) {
    boid->biasval = BIAS_VAL_GROUP1;
  } else if (group_id == 1) {
    boid->biasval = BIAS_VAL_GROUP2;
  }
}

// Update boid positions and velocities based on flocking behavior
void update_boids(GameState *state) {
  for (int i = 0; i < NUM_BOIDS; i++) {
    // Initialize accumulator variables for flocking calculations
    fix15 xpos_avg = 0;
    fix15 ypos_avg = 0;
    fix15 xvel_avg = 0;
    fix15 yvel_avg = 0;
    fix15 neighboring_boids = 0;
    fix15 close_dx = 0;
    fix15 close_dy = 0;

    // Calculate flocking behavior with other boids
    for (int j = 0; j < NUM_BOIDS; j++) {
      if (i == j)
        continue;

      // Calculate distance between boids
      fix15 dx = state->boids[i].x - state->boids[j].x;
      fix15 dy = state->boids[i].y - state->boids[j].y;

      // Approximate distance using Alpha max plus beta min algorithm
      fix15 abs_dx = absfix15(dx);
      fix15 abs_dy = absfix15(dy);
      fix15 distance;
      if (abs_dx > abs_dy) {
        distance = abs_dx + (abs_dy >> 2);
      } else {
        distance = abs_dy + (abs_dx >> 2);
      }

      // Handle collision avoidance (separation)
      if (distance < PROTECTED_RANGE) {
        close_dx += dx;
        close_dy += dy;
      }
      // Handle alignment and cohesion
      else if (distance < VISUAL_RANGE) {
        xpos_avg += state->boids[j].x;
        ypos_avg += state->boids[j].y;
        xvel_avg += state->boids[j].vx;
        yvel_avg += state->boids[j].vy;
        neighboring_boids += int2fix15(1);
      }
    }

    // Apply flocking rules if there are neighboring boids
    if (neighboring_boids > 0) {
      // Calculate averages for alignment and cohesion
      xpos_avg = divfix(xpos_avg, neighboring_boids);
      ypos_avg = divfix(ypos_avg, neighboring_boids);
      xvel_avg = divfix(xvel_avg, neighboring_boids);
      yvel_avg = divfix(yvel_avg, neighboring_boids);

      // Apply centering and velocity matching
      state->boids[i].vx +=
          multfix15(xpos_avg - state->boids[i].x, CENTERING_FACTOR) +
          multfix15(xvel_avg - state->boids[i].vx, MATCHING_FACTOR);

      state->boids[i].vy +=
          multfix15(ypos_avg - state->boids[i].y, CENTERING_FACTOR) +
          multfix15(yvel_avg - state->boids[i].vy, MATCHING_FACTOR);
    }

    // Apply separation force
    state->boids[i].vx += multfix15(close_dx, AVOID_FACTOR);
    state->boids[i].vy += multfix15(close_dy, AVOID_FACTOR);

    // Handle boundary conditions
    if (hitTop(state->boids[i].y)) {
      state->boids[i].vy += TURN_FACTOR;
    }
    if (hitRight(state->boids[i].x)) {
      state->boids[i].vx -= TURN_FACTOR;
    }
    if (hitLeft(state->boids[i].x)) {
      state->boids[i].vx += TURN_FACTOR;
    }
    if (hitBottom(state->boids[i].y)) {
      state->boids[i].vy -= TURN_FACTOR;
    }

    // Apply scout group bias
    if (state->boids[i].scout_group == 0) { // Scout group 1 (biased right)
      if (state->boids[i].vx > 0) {
        state->boids[i].biasval += BIAS_INCREMENT;
        if (state->boids[i].biasval > MAX_BIAS) {
          state->boids[i].biasval = MAX_BIAS;
        }
      } else {
        state->boids[i].biasval -= BIAS_INCREMENT;
        if (state->boids[i].biasval < BIAS_INCREMENT) {
          state->boids[i].biasval = BIAS_INCREMENT;
        }
      }
    } else if (state->boids[i].scout_group ==
               1) { // Scout group 2 (biased left)
      if (state->boids[i].vx < 0) {
        state->boids[i].biasval += BIAS_INCREMENT;
        if (state->boids[i].biasval > MAX_BIAS) {
          state->boids[i].biasval = MAX_BIAS;
        }
      } else {
        state->boids[i].biasval -= BIAS_INCREMENT;
        if (state->boids[i].biasval < BIAS_INCREMENT) {
          state->boids[i].biasval = BIAS_INCREMENT;
        }
      }
    }

    // Apply directional bias based on scout group
    if (state->boids[i].scout_group == 1) {
      state->boids[i].vx = multfix15(int2fix15(1) - state->boids[i].biasval,
                                     state->boids[i].vx) +
                           multfix15(state->boids[i].biasval, int2fix15(1));
    } else if (state->boids[i].scout_group == 2) {
      state->boids[i].vx = multfix15(int2fix15(1) - state->boids[i].biasval,
                                     state->boids[i].vx) +
                           multfix15(state->boids[i].biasval, int2fix15(-1));
    }

    // Calculate and enforce speed limits
    fix15 abs_vx = absfix15(state->boids[i].vx);
    fix15 abs_vy = absfix15(state->boids[i].vy);
    fix15 speed;
    if (abs_vx > abs_vy) {
      speed = abs_vx + (abs_vy >> 2);
    } else {
      speed = abs_vy + (abs_vx >> 2);
    }

    // Enforce maximum speed
    if (speed > MAX_SPEED) {
      state->boids[i].vx =
          multfix15(divfix(state->boids[i].vx, speed), MAX_SPEED);
      state->boids[i].vy =
          multfix15(divfix(state->boids[i].vy, speed), MAX_SPEED);
    }
    // Enforce minimum speed
    if (speed < MIN_SPEED) {
      if (speed == 0) {
        // Give random velocity if speed is zero
        state->boids[i].vx = ((rand() & 0xFFFF) * 3) - int2fix15(3);
        state->boids[i].vy = ((rand() & 0xFFFF) * 3) - int2fix15(3);
        speed = MIN_SPEED;
      }
      state->boids[i].vx =
          multfix15(divfix(state->boids[i].vx, speed), MIN_SPEED);
      state->boids[i].vy =
          multfix15(divfix(state->boids[i].vy, speed), MIN_SPEED);
    }

    // Update boid position
    state->boids[i].x += state->boids[i].vx;
    state->boids[i].y += state->boids[i].vy;
  }
}

// Animate numbers when they collide with boids
void animate_numbers(Number *num, fix15 dx, fix15 dy, fix15 threshold_x,
                     fix15 threshold_y) {
  // Generate random movement for animation
  fix15 shift_x = ((rand() & 0xFFFF) * 3) - int2fix15(3);
  fix15 shift_y = ((rand() & 0xFFFF) * 3) - int2fix15(3);

  // Update position and size
  num->x += fix2int15(shift_x);
  num->y += fix2int15(shift_y);
  if (num->is_bad_number) {
    num->size = 2; // Bad numbers appear larger
  } else {
    num->size = 1;
  }
}

// Check for collisions between boids and numbers, trigger animations
void check_collisions_and_animate(GameState *state) {
  // Convert dimensions to fixed-point for calculations
  fix15 cell_width = int2fix15(CELL_WIDTH);
  fix15 cell_height = int2fix15(CELL_HEIGHT);
  fix15 half_cell_width = divfix(cell_width, int2fix15(2));
  fix15 half_cell_height = divfix(cell_height, int2fix15(2));
  fix15 boid_radius_fix = int2fix15(BOID_COLLISION_RADIUS);

  // Check each cell in the grid
  for (int i = 0; i < ROWS; i++) {
    for (int j = 0; j < COLS; j++) {
      // Calculate cell center
      fix15 cell_center_x = int2fix15(state->state[i][j].x) + half_cell_width;
      fix15 cell_center_y = int2fix15(state->state[i][j].y) + half_cell_height;

      // Check collision with each boid
      for (int k = 0; k < NUM_BOIDS; k++) {
        // Calculate distance between boid and cell center
        fix15 dx = state->boids[k].x - cell_center_x;
        fix15 dy = state->boids[k].y - cell_center_y;

        fix15 abs_dx = absfix15(dx);
        fix15 abs_dy = absfix15(dy);

        fix15 threshold_x = half_cell_width + boid_radius_fix;
        fix15 threshold_y = half_cell_height + boid_radius_fix;

        // Check for collision and trigger animation
        if ((abs_dx < threshold_x) && (abs_dy < threshold_y)) {
          animate_numbers(&state->state[i][j], dx, dy, threshold_x,
                          threshold_y);

          // Mark which boid triggered the animation
          if (k == 0) {
            state->state[i][j].animated_last_frame_by_boid0 = 1;
          } else if (k == 1) {
            state->state[i][j].animated_last_frame_by_boid1 = 1;
          }
          break;
        }
      }
    }
  }
}

// Handle cursor interaction with numbers and update game state
void handle_cursor_refinement(GameState *state) {
  // Convert cursor position to grid coordinates
  int grid_row = (state->cursor.y - GRID_START_Y) / CELL_HEIGHT;
  int grid_col = (state->cursor.x - GRID_START_X) / CELL_WIDTH;

  // Validate grid position
  if (!is_valid(grid_row, grid_col)) {
    return;
  }

  Number *num = &state->state[grid_row][grid_col];

  // Handle interaction with boid0 (first scout group)
  if (num->is_bad_number && num->animated_last_frame_by_boid0 == 1) {
    int bin_id = num->bad_number.bin_id;
    num->number = 0;
    num->refined_last_frame = 1;
    state->total_bad_numbers--;

    // Trigger animations
    state->box_anims[bin_id].anim_state = ANIM_GROWING;
    state->progress_bar.anim_state = ANIMATION_GROWING;

    // Mark all affected numbers as refined
    for (int i = 0; i < ROWS; i++) {
      for (int j = 0; j < COLS; j++) {
        if (state->state[i][j].animated_last_frame_by_boid0 == 1) {
          state->state[i][j].refined_last_frame = 1;
        }
      }
    }
  }
  // Handle interaction with boid1 (second scout group)
  else if (num->is_bad_number && num->animated_last_frame_by_boid1 == 1) {
    int bin_id = num->bad_number.bin_id;
    int value = num->number;
    num->number = 0;
    num->refined_last_frame = 1;
    state->total_bad_numbers--;

    // Update box percentages
    state->box_anims[bin_id].woe_percentage += value;
    state->box_anims[bin_id].frolic_percentage += value;
    state->box_anims[bin_id].dread_percentage += value;
    state->box_anims[bin_id].malice_percentage += value;

    // Trigger animations
    state->box_anims[bin_id].anim_state = ANIM_GROWING;
    state->progress_bar.anim_state = ANIMATION_GROWING;

    // Mark all affected numbers as refined
    for (int i = 0; i < ROWS; i++) {
      for (int j = 0; j < COLS; j++) {
        if (state->state[i][j].animated_last_frame_by_boid1 == 1) {
          state->state[i][j].refined_last_frame = 1;
        }
      }
    }
  }
}