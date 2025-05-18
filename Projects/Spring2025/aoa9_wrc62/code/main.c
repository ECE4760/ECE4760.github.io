/**
 * Hunter Adams (vha3@cornell.edu), Akinfolami Akin-Alamu(aoa9), Wilson Coronel
 * (wrc62)
 *
 * HARDWARE CONNECTIONS
 *  - GPIO 16 ---> VGA Hsync
 *  - GPIO 17 ---> VGA Vsync
 *  - GPIO 18 ---> 470 ohm resistor ---> VGA Green
 *  - GPIO 19 ---> 330 ohm resistor ---> VGA Green
 *  - GPIO 20 ---> 330 ohm resistor ---> VGA Blue
 *  - GPIO 21 ---> 330 ohm resistor ---> VGA Red
 *  - RP2040 GND ---> VGA GND
 *
 * RESOURCES USED
 *  - PIO state machines 0, 1, and 2 on PIO instance 0
 *  - DMA channels obtained by claim mechanism
 *  - 153.6 kBytes of RAM (for pixel color data)
 *
 * Protothreads v1.1.3
 * Threads:
 * core 0:
 * Graphics demo
 * blink LED25
 * core 1:
 * Toggle gpio 4
 * Serial i/o
 */
// ==========================================
// === VGA graphics library
// ==========================================
#include "game_state.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "vga16_graphics.h"
#include <assert.h> // For assert
#include <math.h>
#include <stdio.h>
#include <stdlib.h> // For abs() function
#include <string.h> // For strlen
// // Our assembled programs:
// // Each gets the name <pio_filename.pio.h>
// #include "hsync.pio.h"
// #include "vsync.pio.h"
// #include "rgb.pio.h"
#include "game_state.h"

// ==========================================
// === protothreads globals
// ==========================================
#include "hardware/adc.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/multicore.h"

#include "string.h"
// protothreads header
#include "pt_cornell_rp2040_v1_3.h"

#define FRAME_RATE 60000 // 60 FPS

int ADC_GPIO_VX = 28;
int ADC_GPIO_VY = 27;
int BUTTON_PIN = 22;

int X_RIGHT_THRESHOLD = 3000;
int X_LEFT_THRESHOLD = 500;

int Y_DOWN_THRESHOLD = 3000;
int Y_UP_THRESHOLD = 500;
int RIGHT_MARGIN_GRID = GRID_START_X + (COLS * CELL_WIDTH);
int LEFT_MARGIN_GRID = GRID_START_X;
int TOP_MARGIN_GRID = GRID_START_Y;
int BOTTOM_MARGIN_GRID = GRID_START_Y + (ROWS * CELL_HEIGHT);

GameState game_state;
// semaphore
static struct pt_sem start_game_sem;

// ==================================================
// === Lumon logo drawing function
// Draws the Lumon logo at specified position with given dimensions:
// - Creates concentric ovals for the globe
// - Adds horizontal lines for the logo structure
// - Displays "LUMON" text in the center
// ==================================================
void draw_lumon_logo(int cx, int cy, int logo_w, int logo_h) {
  char fill_color = DARK_BLUE; // Dark blue for the globe fill
  char line_color = WHITE;     // Bright white for outlines and text

  // Calculate radii for the concentric ovals
  short outer_rx = logo_w / 2;
  short ry = logo_h / 2;
  short middle_rx = (short)(outer_rx * 0.75);
  short inner_rx = (short)(outer_rx * 0.5);

  // Draw the concentric ovals
  drawOval(cx, cy, outer_rx, ry, line_color);  // Outer oval
  drawOval(cx, cy, middle_rx, ry, line_color); // Middle oval
  drawOval(cx, cy, inner_rx, ry, line_color);  // Inner oval

  // Draw horizontal lines
  drawHLine(cx - middle_rx, cy - (ry - 5), logo_w * 0.75, line_color);
  drawHLine(cx - middle_rx, cy + (ry - 5), logo_w * 0.75, line_color);

  // Draw LUMON text
  char text_str[] = "LUMON";
  setCursor(cx - (middle_rx + 2), cy - 5);
  setTextSize(2);
  setTextColor(WHITE);
  writeString(text_str);
}

// ==================================================
// === Box drawing function
// Draws boxes with index and progress bar:
// - Top rectangle shows box index
// - Bottom rectangle shows progress percentage
// - Includes percentage text display
// ==================================================
void draw_boxes(int x, int y, int w, int h, int percentage, int idx) {
  // Format box index as two-digit string
  char index[3] = {0, 0, 0};
  index[0] = '0';
  index[1] = '0' + idx;

  // Draw top rectangle with index
  drawRect(x, y, w, h, CYAN);
  setCursor(x + (w / 2) - 4, y + (h / 2) - 4);
  setTextSize(1);
  setTextColor(WHITE);
  writeString(index);

  // Draw bottom progress bar
  int bottom_y = y + h + 2;
  drawRect(x, bottom_y, w, h, CYAN);

  // Calculate and draw filled portion
  int fill_w = (w * percentage) / 100;
  if (fill_w > w)
    fill_w = w;
  if (fill_w < 0)
    fill_w = 0;

  if (fill_w > 0) {
    fillRect(x, bottom_y, fill_w, h, WHITE);
  }

  // Draw percentage text
  char percent_str[5];
  sprintf(percent_str, "%d%%", percentage);
  int text_width = strlen(percent_str) * 6;
  int text_x = x + 5;
  int text_y = bottom_y + (h / 2) - 4;

  setCursor(text_x, text_y);
  setTextSize(1);
  setTextColor(BLACK);
  writeString(percent_str);
}

// ==================================================
// === Woe/Frolic/Dread/Malice percentage display
// Draws percentage bars for each emotion category:
// - Shows WO, FC, DR, MA labels
// - Displays progress bars for each emotion
// - Updates based on current animation state
// ==================================================
void draw_woe_frolic_dread_malice_percentages(Box *box, BoxAnim *anim) {
  // Calculate positions and dimensions
  int top_of_anim_box_y = box->y - anim->current_anim_height;
  int y_offset = 5;
  int label_x_offset = box->x + 5;
  int label_width = 2 * 6;
  int bar_x_offset = label_x_offset + label_width + 4;
  int bar_max_width = box->width - (bar_x_offset - box->x) - 5;
  int bar_height = 6;

  // Define emotion categories and their percentages
  char titles[4][3] = {"WO", "FC", "DR", "MA"};
  int percentages[4] = {anim->woe_percentage, anim->frolic_percentage,
                        anim->dread_percentage, anim->malice_percentage};

  // Layout parameters
  int text_height = 8;
  int line_spacing = 2;

  // Ensure minimum bar width
  if (bar_max_width < 10)
    bar_max_width = 10;

  // Draw each emotion category
  for (int i = 0; i < 4; i++) {
    int current_y =
        top_of_anim_box_y + y_offset + (i * (text_height + line_spacing));
    int bar_center_y = current_y + (text_height / 2) - (bar_height / 2);

    // Check if within animation box bounds
    if (current_y < box->y && current_y >= top_of_anim_box_y &&
        (current_y + text_height) <= box->y) {
      // Draw label
      setCursor(label_x_offset, current_y);
      setTextSize(1);
      setTextColor(WHITE);
      writeString(titles[i]);

      // Draw progress bar outline
      drawRect(bar_x_offset, bar_center_y, bar_max_width, bar_height, WHITE);

      // Calculate and draw filled portion
      int fill_w = (bar_max_width * percentages[i]) / 100;
      if (fill_w < 0)
        fill_w = 0;
      if (fill_w > bar_max_width)
        fill_w = bar_max_width;

      if (fill_w > 0) {
        fillRect(bar_x_offset, bar_center_y, fill_w, bar_height, WHITE);
      }
    }
  }
}

// ==================================================
// === Button press handling thread
// Implements debouncing logic and handles button press events:
// - Debounces button input to prevent false triggers
// - Handles cursor refinement on button press
// - Manages game state transitions
// ==================================================
static PT_THREAD(protothread_button_press(struct pt *pt)) {
  PT_BEGIN(pt);
  // Debounce state machine states
  static enum {
    NOT_PRESSED,
    MAYBE_PRESSED,
    PRESSED,
    MAYBE_NOT_PRESSED
  } debounce_state = NOT_PRESSED;

  static int possible_press = -1;

  while (1) {
    bool buttonpress = gpio_get(BUTTON_PIN);

    // State machine for button debouncing
    switch (debounce_state) {
    case NOT_PRESSED:
      if (buttonpress == 0) {
        possible_press = buttonpress;
        debounce_state = MAYBE_PRESSED;
      }
      break;
    case MAYBE_PRESSED:
      if (buttonpress == 0 && possible_press == 0) {
        handle_cursor_refinement(&game_state);
        debounce_state = PRESSED;
      }
      if (game_state.play_state == START_SCREEN) {
        game_state.play_state = PLAYING;
        PT_SEM_SAFE_SIGNAL(pt, &start_game_sem);
      }
      break;
    case PRESSED:
      if (buttonpress == 1)
        debounce_state = MAYBE_NOT_PRESSED;
      break;
    case MAYBE_NOT_PRESSED:
      if (buttonpress == 0)
        debounce_state = PRESSED;
      else
        debounce_state = NOT_PRESSED;
      break;
    default:
      break;
    }

    // Schedule next check in 30ms
    PT_YIELD_usec(30000);
  }

  PT_END(pt);
}

void draw_cursor(GameState *state, int color) {
  // int x = state->cursor.x + (CELL_WIDTH / 2);
  // int y = state->cursor.y;
  // int size = 12;

  // // Draw diagonal lines at top
  // for (int i = 0; i < size / 2; i++) {
  //   drawPixel(x - i, y + i, color);
  //   drawPixel(x + i, y + i, color);
  // }

  // // Draw main pointer
  // drawVLine(x, y, size, color);
  drawRect(game_state.cursor.x, game_state.cursor.y, game_state.cursor.width,
           game_state.cursor.height, color);

  // Draw horizontal line
  // drawHLine(x - size / 2, y + size, size, color);
}

int get_VX_ADC() {
  adc_select_input(2);
  return adc_read();
}

int get_VY_ADC() {
  adc_select_input(1);
  return adc_read();
}

// ==================================================
// === Joystick control thread
// Handles joystick input and cursor movement:
// - Reads analog values from joystick
// - Updates cursor position based on input
// - Ensures cursor stays within grid boundaries
// ==================================================
static PT_THREAD(protothread_joystick(struct pt *pt)) {
  PT_BEGIN(pt);

  while (1) {
    // Read joystick values
    int x_value = get_VX_ADC();
    int y_value = get_VY_ADC();

    // Erase previous cursor position
    draw_cursor(&game_state, BLACK);

    // Handle joystick movement
    if (x_value > X_RIGHT_THRESHOLD) {
      // Move cursor right
      game_state.cursor.x += CELL_WIDTH;
      if (game_state.cursor.x > RIGHT_MARGIN_GRID)
        game_state.cursor.x = RIGHT_MARGIN_GRID;
    } else if (x_value < X_LEFT_THRESHOLD) {
      // Move cursor left
      game_state.cursor.x -= CELL_WIDTH;
      if (game_state.cursor.x < LEFT_MARGIN_GRID)
        game_state.cursor.x = LEFT_MARGIN_GRID;
    } else if (y_value > Y_DOWN_THRESHOLD) {
      // Move cursor down
      game_state.cursor.y += CELL_HEIGHT;
      if (game_state.cursor.y > BOTTOM_MARGIN_GRID)
        game_state.cursor.y = BOTTOM_MARGIN_GRID;
    } else if (y_value < Y_UP_THRESHOLD) {
      // Move cursor up
      game_state.cursor.y -= CELL_HEIGHT;
      if (game_state.cursor.y < TOP_MARGIN_GRID)
        game_state.cursor.y = TOP_MARGIN_GRID;
    }

    // Draw new cursor position
    draw_cursor(&game_state, CYAN);

    // Schedule next update in 70ms
    PT_YIELD_usec(70000);
  }

  PT_END(pt);
}

// ==================================================
// === Progress bar animation thread
// Manages the progress bar animation and game completion:
// - Updates progress bar based on game state
// - Handles game completion when progress reaches 100%
// ==================================================
static PT_THREAD(protothread_progress_bar(struct pt *pt)) {
  PT_BEGIN(pt);
  static int begin_time;
  static int spare_time;
  static int new_progress;
  static int bad_numbers;

  while (1) {
    begin_time = time_us_32();
    ProgressBarAnimation *progress_bar = &game_state.progress_bar;

    // Handle progress bar animation states
    switch (progress_bar->anim_state) {
    case ANIMATION_IDLE:
      break;
    case ANIMATION_GROWING:
      // Calculate progress increment based on remaining bad numbers
      bad_numbers =
          game_state.total_bad_numbers == 0 ? 1 : game_state.total_bad_numbers;
      new_progress = (100 - progress_bar->current_progress) / bad_numbers;
      progress_bar->current_progress += new_progress;

      // Check for game completion
      if (progress_bar->current_progress >= 100) {
        progress_bar->current_progress = 100;
        game_state.play_state = GAME_WON;
      }
      progress_bar->anim_state = ANIMATION_IDLE;
      break;
    }

    // Calculate time to next frame
    spare_time = FRAME_RATE - (time_us_32() - begin_time);
    if (spare_time < 0)
      spare_time = 0;
    PT_YIELD_usec(spare_time);
  }
  PT_END(pt);
}

// ==================================================
// === graphics demo -- RUNNING on core 0
// ==================================================
static PT_THREAD(protothread_graphics(struct pt *pt)) {
  PT_BEGIN(pt);
  static int begin_time;
  static int spare_time;
  static char progress_str[15]; // Declare the progress string buffer

  // ---- To Start the game; user has to press some button ---- //
  // Write the instructions on the screen
  setCursor(200, 240);
  setTextSize(2);
  setTextColor(YELLOW);
  writeString("Press button to start!");
  PT_SEM_WAIT(pt, &start_game_sem);

  game_state_init(&game_state, time_us_32());

  // Variables for grid drawing
  static const int cell_width = 40;   // Width of each grid cell
  static const int cell_height = 40;  // Height of each grid cell
  static const int grid_start_x = 10; // Starting X position of the grid
  static int grid_start_y = 60;       // Starting Y position of the grid
  static char num_str[2] = {
      0, 0}; // String to hold the number (plus null terminator)

  // Clear the screen first
  fillRect(0, 0, 640, 480, BLACK);

  // Progress bar
  int progress_bar_width = (COLS * cell_width);
  int progress_bar_y = 20;
  int progress_bar_height = 30;
  int progress_bar_x = grid_start_x + 10;

  // Lumon Logo - to the right of the progress bar
  int logo_w = 70;
  int logo_h = 40;
  int logo_cx = progress_bar_width - 10;
  int logo_cy = progress_bar_y + (progress_bar_height / 2);

  draw_lumon_logo(logo_cx, logo_cy, logo_w, logo_h);

  // Reset text properties for grid numbers
  setTextSize(1);
  setTextColor2(WHITE, BLACK);

  grid_start_y = progress_bar_y + progress_bar_height + 30; // Start grid below
  // draw straight line at the top
  // drawHLine(grid_start_x, grid_start_y, COLS * cell_width, CYAN);
  // // draw straight line at the bottom
  // drawHLine(grid_start_x, grid_start_y + ((ROWS + 1) * cell_height),
  //           COLS * cell_width, CYAN);
  // move grid down

  // Draw final botton box
  fillRect(grid_start_x, 460, COLS * cell_width, 10, CYAN);
  setCursor(((COLS * cell_width) / 2) - 40, 460 + 1);
  setTextColor(BLACK);
  setTextSize(1);
  writeString("0x5D9EA : 0xB57135");

  int wfdm_width = 60;
  int wfdm_height = 10;
  int wfdm_y = 420;
  int wfdm_start_x = 40;

  // int random_index = rand() % 4;
  // game_state.box_anims[random_index].anim_state = ANIM_GROWING;

  setCursor(progress_bar_x + 10, progress_bar_y + 10);
  setTextColor(RED);
  setTextSize(2);
  writeString("Ocula");

  while (true) {
    begin_time = time_us_32();
    int progress_bar_fill_width =
        (progress_bar_width * game_state.progress_bar.current_progress) / 100;

    // Progress bar
    drawRect(progress_bar_x, progress_bar_y, progress_bar_width,
             progress_bar_height, CYAN);
    fillRect(progress_bar_x, progress_bar_y, progress_bar_fill_width,
             progress_bar_height, WHITE); // WHITE fill based on progress

    // Draw Ocula text on top of the progress bar
    setCursor(progress_bar_x + 10, progress_bar_y + 10);
    setTextColor(RED);
    setTextSize(2);
    writeString("Ocula");

    // Draw percentage
    char percent_str[5];
    sprintf(percent_str, "%d%%", game_state.progress_bar.current_progress);
    setTextColor(DARK_BLUE);
    setCursor(progress_bar_x + progress_bar_fill_width + 5,
              progress_bar_y + 10);
    setTextSize(2);
    writeString(percent_str);

    // Reset number positions, sizes, and animation flags before collision
    for (int row = 0; row < ROWS; row++) {
      for (int col = 0; col < COLS; col++) {

        if (game_state.state[row][col].refined_last_frame == 1) {
          int random_number = rand();
          game_state.state[row][col].number = random_number % 10;
          game_state.state[row][col].is_bad_number = (random_number & 0xF) > 14;
          game_state.state[row][col].bad_number.bin_id = random_number % 4;
          if (game_state.state[row][col].is_bad_number) {
            game_state.total_bad_numbers++;
          }
        }

        if (game_state.state[row][col].animated_last_frame_by_boid0 == 1 ||
            game_state.state[row][col].animated_last_frame_by_boid1 == 1) {
          // Clear the area with the correct size
          fillRect(game_state.state[row][col].x, game_state.state[row][col].y,
                   CELL_WIDTH, CELL_HEIGHT, BLACK);
        }
        game_state.state[row][col].x = GRID_START_X + (col * CELL_WIDTH);
        game_state.state[row][col].y = GRID_START_Y + (row * CELL_HEIGHT);
        game_state.state[row][col].size = 1;
        game_state.state[row][col].animated_last_frame_by_boid0 = 0;
        game_state.state[row][col].animated_last_frame_by_boid1 = 0;
        game_state.state[row][col].refined_last_frame = 0;
      }
    }

    // Update the boids
    update_boids(&game_state);

    // Check collisions and mark numbers for animation
    check_collisions_and_animate(&game_state);

    // Draw the numbers from the game state
    for (int row = 0; row < ROWS; row++) {
      for (int col = 0; col < COLS; col++) {
        // convert number to string
        num_str[0] = '0' + game_state.state[row][col].number;

        // set text properties
        setCursor(game_state.state[row][col].x + CELL_WIDTH / 2,
                  game_state.state[row][col].y +
                      CELL_HEIGHT / 2); // center text in cell

        if (game_state.state[row][col].is_bad_number) {
          setTextColor(RED);
        } else {
          setTextColor(WHITE);
        }

        setTextSize(game_state.state[row][col].size);

        // draw the number
        writeString(num_str);
      }
    }

    //  update the game state
    for (int i = 0; i < 5; i++) {
      game_state_update_boxes(&game_state.boxes[i],
                              wfdm_start_x + (wfdm_width + 60) * i, wfdm_y,
                              wfdm_width, wfdm_height, 50);
    }

    // Draw the woe frolic dread and malice boxes
    for (int i = 0; i < 5; i++) {
      draw_boxes(game_state.boxes[i].x, game_state.boxes[i].y,
                 game_state.boxes[i].width, game_state.boxes[i].height,
                 game_state.boxes[i].percentage, i);
    }

    spare_time = FRAME_RATE - (time_us_32() - begin_time);
    PT_YIELD_usec(spare_time);
  }
  PT_END(pt);
} // graphics thread

// ==================================================
// === box animation thread on core 0
// ==================================================
// the on-board LED blinks
static PT_THREAD(protothread_graphics_too(struct pt *pt)) {
  PT_BEGIN(pt);
  static int spare_time;
  static int begin_time;
  static int i;
  static BoxAnim *anim;
  static Box *box;

  while (1) {
    begin_time = time_us_32();
    for (i = 0; i < 5; i++) {
      anim = &game_state.box_anims[i];
      box = &game_state.boxes[i];
      switch (anim->anim_state) {
      case ANIM_GROWING:
        drawRect(box->x, box->y - anim->current_anim_height, box->width,
                 BOX_ANIM_INCREMENT, BLACK);
        anim->current_anim_height += BOX_ANIM_INCREMENT;
        if (anim->current_anim_height >= BOX_ANIM_MAX_HEIGHT) {
          anim->current_anim_height = BOX_ANIM_MAX_HEIGHT;
          // Draw the final, fully grown box
          drawRect(box->x, box->y - anim->current_anim_height, box->width,
                   anim->current_anim_height, CYAN);
          // Wait a 3s before transitioning to shrinking
          draw_woe_frolic_dread_malice_percentages(box, anim);
          PT_YIELD_usec(3000000);
          // Clear the box area before transitioning to shrinking
          anim->anim_state = ANIM_SHRINKING;
        } else {
          // Draw the growing box
          drawRect(box->x, box->y - anim->current_anim_height, box->width,
                   anim->current_anim_height, CYAN);
        }
        break;
      case ANIM_SHRINKING:
        // Clear the top strip that's disappearing this frame
        fillRect(box->x, box->y - anim->current_anim_height, box->width,
                 BOX_ANIM_INCREMENT, // Height of the strip to clear
                 BLACK);
        anim->current_anim_height -= BOX_ANIM_INCREMENT;
        if (anim->current_anim_height <= 0) {
          anim->current_anim_height = 0;
          anim->anim_state = ANIM_IDLE;
        } else {
          // Draw the shrinking box
          drawRect(box->x, box->y - anim->current_anim_height, box->width,
                   anim->current_anim_height, CYAN);
        }
        break;

      case ANIM_IDLE:
        break;
      }
    }

    // NEVER exit while
    spare_time = FRAME_RATE - (time_us_32() - begin_time);
    PT_YIELD_usec(spare_time);
  } // END WHILE(1)
  PT_END(pt);
} // blink thread

// ========================================
// === Core 1 main function
// Initializes and starts threads on core 1:
// - Graphics animation thread
// - Progress bar thread
// ========================================
void core1_main() {
  // Add threads to core 1 scheduler
  pt_add_thread(protothread_graphics_too);
  pt_add_thread(protothread_progress_bar);
  pt_schedule_start;
}

// ========================================
// === Main function
// Initializes hardware and starts all threads:
// 1. Sets up button input
// 2. Configures joystick ADC
// 3. Initializes VGA display
// 4. Launches core 1
// 5. Sets up core 0 threads
// ========================================
int main() {
  // Initialize standard I/O
  stdio_init_all();

  // Configure button input with pull-up
  gpio_init(BUTTON_PIN);
  gpio_set_dir(BUTTON_PIN, GPIO_IN);
  gpio_pull_up(BUTTON_PIN);

  // Configure joystick analog inputs
  adc_gpio_init(ADC_GPIO_VX);
  adc_gpio_init(ADC_GPIO_VY);
  adc_init();
  adc_select_input(0);

  // Initialize VGA display
  initVGA();

  // Launch core 1 with its threads
  multicore_reset_core1();
  multicore_launch_core1(&core1_main);

  // Configure threads for core 0
  pt_add_thread(protothread_graphics);     // Main graphics thread
  pt_add_thread(protothread_joystick);     // Joystick input thread
  pt_add_thread(protothread_button_press); // Button input thread

  // Start the scheduler
  pt_schedule_start;
  // Program never exits
} // end main