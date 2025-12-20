/** snake_config.h
 * Compile-time configuration of the game's code.
 */

#ifndef __GAME__H__
#define __GAME__H__

/* SIZES */

// Integer pixel values
#define HEAD_RADIUS 4
#define PELLET_RADIUS 2
#define SEGMENT_LEN 3

/* SERVER-AGENT COMMUNICATION */

#define NUM_AGENTS            1
#define FIRST_AGENT_ID        2
#define LAST_AGENT_ID         ((FIRST_AGENT_ID) + (NUM_AGENTS) - 1)

/* GAMEPLAY STARTING CONDITIONS */

// True/False
#define START_CONTROLLING     0
// Float
#define START_TIMESCALE       1.0f

/* DRAWING CONSTANTS */

// True/False
#define SHOW_SEG_JOINTS       0

#define SHOW_HVEC             1
#define SHOW_VVEC             0
#define SHOW_SID              0

// color_t
#define HVEC_COLOR            DARK_ORANGE
#define VVEC_COLOR            MAGENTA
#define SID_COLOR             WHITE
// color_t
#define BORDER_COLOR          WHITE

/* GAME-STATE & PHYSICS */

// Names for SNAKE_SCORE_METHOD
#define SNAKE_ONE             0
#define SNAKE_FIB             1
#define SNAKE_LOG2            2

// 0, 1, or 2 (see above)
// 0 -> 1-to-1, 1 -> Fibonacci sequence, 2 -> log2
#define SNAKE_SCORE_METHOD    SNAKE_ONE

// Bounded integers (loosely checked at compile-time)
#define MAX_PELLETS           5
#define MAX_SNAKE_SEGMENTS    150
#define MAX_SNAKES            2

// Fix15
// Distances are in pixels
// Angles are in radians
// Velocities are px/ms

#define SNAKE_SPEED           1000    // px/ms
#define SNAKE_SPEED_MAX       1500    // px/ms
#define SNAKE_SPEED_MIN       500    // px/ms
#define TURN_SPEED            150     // rad/ms

#endif  //!__GAME__H__