// Include the VGA grahics library
#include "vga16_graphics.h"

// Include standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

// Include Pico libraries
#include "pico/stdlib.h"
#include "pico/divider.h"
#include "pico/multicore.h"

// Include hardware libraries
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/irq.h"
#include "hardware/adc.h"
#include "hardware/pio.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"

// Include protothreads
#include "mpu6050.h"
#include "pt_cornell_rp2040_v1_3.h"

// === the fixed point macros ========================================
typedef signed int fix15;
#define multfix15(a, b) ((fix15)((((signed long long)(a)) * ((signed long long)(b))) >> 15))
#define float2fix15(a) ((fix15)((a) * 32768.0)) // 2^15
#define fix2float15(a) ((float)(a) / 32768.0)
#define absfix15(a) abs(a)
#define int2fix15(a) ((fix15)(a << 15))
#define fix2int15(a) ((int)(a >> 15))
#define char2fix15(a) (fix15)(((fix15)(a)) << 15)
#define divfix(a, b) (fix15)(div_s64s64((((signed long long)(a)) << 15), ((signed long long)(b))))

// Wall detection
#define hitBottom(b) (b > int2fix15(480))
#define hitTop(b) (b < int2fix15(0))
#define hitLeft(a) (a < int2fix15(0))
#define hitRight(a) (a > int2fix15(640))

// uS per frame
#define FRAME_RATE 33000

// the color of the boid
char color = WHITE;

// Boid on core 0
fix15 boid0_x;
fix15 boid0_y;
fix15 boid0_vx;
fix15 boid0_vy;

// Boid on core 1
fix15 boid1_x;
fix15 boid1_y;
fix15 boid1_vx;
fix15 boid1_vy;

// Score and Variable Table
volatile int Score_A = 0;
volatile int Score_B = 0;
volatile int WinningScore_A = 0;
volatile int WinningScore_B = 0;

// Arrays in which the raw measurements will be stored
fix15 acceleration[3], gyro[3];
fix15 acceleration1[3], gyro1[3];
fix15 accelA[3], gyroA[3], accelB[3], gyroB[3];

// ————————————————————————————————————————————————————————————————————————————
//  CONFIGURATION
// ————————————————————————————————————————————————————————————————————————————
#define SCREEN_W 640
#define SCREEN_H 480
#define PADDLE_W 10
#define PADDLE_H 50

// MPU addresses (tie AD0 low for 0x68, high for 0x69)
//#define MPU_ADDR_A 0x68
// #define MPU_ADDR_B   0x69

// keep track of last‑drawn paddle Y’s
static int lastY_A = (SCREEN_H - PADDLE_H) / 2;
static int lastY_B = (SCREEN_H - PADDLE_H) / 2;

// Create a boid
void spawnBoid(fix15 *x, fix15 *y, fix15 *vx, fix15 *vy, int direction)
{
  // Start in center of screen
  *x = int2fix15(320);
  *y = int2fix15(240);
  // Choose left or right
  if (direction)
    *vx = int2fix15(3);
  else
    *vx = int2fix15(-3);
  // Moving down
  *vy = int2fix15(1);
}

// maps raw accel Y (≈–1..+1 g in fix15) → pixel [0..SCREEN_H–PADDLE_H]
static int mapAccelY_to_pixel(fix15 rawY)
{
  float g = fix2float15(rawY);
  if (g < -1.f)
    g = -1.f;
  else if (g > 1.f)
    g = 1.f;
  // shift [–1..+1] → [0..1], scale
  return (int)((g + 1.f) * 0.5f * (SCREEN_H - PADDLE_H));
}

// call this once per frame instead of drawPaddle()
void update_paddles_from_imus()
{
  int newY_A, newY_B;
  // — read IMU A —
  // mpu6050_init(i2c0, MPU_ADDR_A);      // point driver at 0x68
  mpu6050_read_raw(accelA, gyroA);
  newY_A = mapAccelY_to_pixel(accelA[1]);

  // — read IMU B —
  // mpu6050_init(i2c0, MPU_ADDR_B);      // point driver at 0x69
  // mpu6050_read_raw(accelB, gyroB);
  // newY_B = mapAccelY_to_pixel(accelB[1]);

  // — erase old paddles —
  fillRect(0, lastY_A, PADDLE_W, PADDLE_H, BLACK);
  // fillRect(SCREEN_W - PADDLE_W, lastY_B, PADDLE_W, PADDLE_H, BLACK);

  // — draw updated paddles —
  fillRect(0, newY_A, PADDLE_W, PADDLE_H, RED);
  // fillRect(SCREEN_W - PADDLE_W, newY_B, PADDLE_W, PADDLE_H, RED);

  // — remember for next frame —
  lastY_A = newY_A;
  // lastY_B = newY_B;
}

// Draw the boundaries
void drawArena()
{
  drawVLine(320, 0, 480, WHITE);
  drawCircle(320, 240, 100, GREEN);
  setTextColor(WHITE);
  setTextSize(0.2);
  setCursor(10, 10);
  writeString("Player A ");

  char buffer[12];
  sprintf(buffer, "%d", Score_A);
  setCursor(10, 20);
  fillRect(10, 20, 15, 7, BLACK);
  writeString(buffer);
  setCursor(590, 10);
  writeString("Player B ");

  char buffer1[12];
  sprintf(buffer1, "%d", Score_B);
  setCursor(620, 20);
  fillRect(620, 20, 15, 7, BLACK);
  writeString(buffer1);

  char buffer2[12];
  sprintf(buffer2, "%d", WinningScore_A);
  setCursor(305, 10);
  fillRect(305, 10, 15, 7, BLACK);
  writeString(buffer2);

  char buffer3[12];
  sprintf(buffer3, "%d", WinningScore_B);
  setCursor(330, 10);
  fillRect(330, 10, 15, 7, BLACK);
  writeString(buffer3);

  // drawVLine(540, 100, 280, WHITE) ;
  // drawHLine(100, 100, 440, WHITE) ;
  // drawHLine(100, 380, 440, WHITE) ;
}

void reset_board()
{
  fillRect(0, 0, 620, 480, BLACK);
}

void drawPaddle()
{
  fillRect(0, 100, 10, 50, RED);
  fillRect(630, 150, 10, 50, RED);
}

void calculateScore()
{
  int diff1 = Score_A - Score_B;
  int diff2 = Score_B - Score_A;
  if (hitRight(boid0_x))
  {
    Score_A++;
    if ((Score_A == 11 && Score_B <= 9) || (Score_A > 11 && diff1 == 2))
    {
      Score_A = 0;
      Score_B = 0;
      WinningScore_A++;
      if (WinningScore_A == 3)
      {
        WinningScore_B = 0;
        WinningScore_A = 0;
        // Do Nothing - Print A Wins and Reset the Board
        setTextColor(WHITE);
        setTextSize(1);
        setCursor(280, 240);
        writeString("Player A Wins");
        sleep_ms(2000);
        reset_board();
      }
    }
  }
  if (hitLeft(boid0_x))
  {
    Score_B++;
    if ((Score_B == 11 && Score_A <= 9) || (Score_B > 11 && diff2 == 2))
    {
      Score_A = 0;
      Score_B = 0;
      WinningScore_B++;
      if (WinningScore_B == 3)
      {
        WinningScore_B = 0;
        WinningScore_A = 0;
        // Do Nothing - Print A Wins and Reset the Board
        setTextColor(WHITE);
        setTextSize(1);
        setCursor(280, 240);
        writeString("Player B Wins");
        sleep_ms(2000);
        reset_board();
      }
    }
  }
}

// call once at startup (before you launch cores/threads)
void init_two_mpus()
{
  // initialize I2C0 at 400 kHz on GP4=SDA, GP5=SCL:
  i2c_init(i2c0, 400 * 1000);
  gpio_set_function(4, GPIO_FUNC_I2C);
  gpio_set_function(5, GPIO_FUNC_I2C);
  // gpio_pull_up(4); gpio_pull_up(5);

  mpu6050_reset();
  mpu6050_read_raw(acceleration, gyro);
  // configure both MPUs once each
  // mpu6050_init(i2c0, MPU_ADDR_A);
  // mpu6050_init(i2c0, MPU_ADDR_B);
}

// Detect wallstrikes, update velocity and position
void wallsAndEdges(fix15 *x, fix15 *y, fix15 *vx, fix15 *vy)
{
  // Reverse direction if we've hit a wall
  if (hitTop(*y))
  {
    *vy = (-*vy);
    *y = (*y + int2fix15(5));
  }
  if (hitBottom(*y))
  {
    *vy = (-*vy);
    *y = (*y - int2fix15(5));
  }
  if (hitRight(*x))
  {
    *vx = (-*vx);
    *x = (*x - int2fix15(5));
  }
  if (hitLeft(*x))
  {
    *vx = (-*vx);
    *x = (*x + int2fix15(5));
  }

  // Update position using velocity
  *x = *x + *vx;
  *y = *y + *vy;
}

// ==================================================
// === users serial input thread
// ==================================================
static PT_THREAD(protothread_serial(struct pt *pt))
{
  PT_BEGIN(pt);
  // stores user input
  static int user_input;
  // wait for 0.1 sec
  PT_YIELD_usec(1000000);
  // announce the threader version
  sprintf(pt_serial_out_buffer, "Protothreads RP2040 v1.0\n\r");
  // non-blocking write
  serial_write;
  while (1)
  {
    // print prompt
    sprintf(pt_serial_out_buffer, "input a number in the range 1-15: ");
    // non-blocking write
    serial_write;
    // spawn a thread to do the non-blocking serial read
    serial_read;
    // convert input string to number
    sscanf(pt_serial_in_buffer, "%d", &user_input);
    // update boid color
    if ((user_input > 0) && (user_input < 16))
    {
      color = (char)user_input;
    }
  } // END WHILE(1)
  PT_END(pt);
} // timer thread

// Animation on core 0
static PT_THREAD(protothread_anim(struct pt *pt))
{
  // Mark beginning of thread
  PT_BEGIN(pt);

  // Variables for maintaining frame rate
  static int begin_time;
  static int spare_time;

  // Spawn a boid
  spawnBoid(&boid0_x, &boid0_y, &boid0_vx, &boid0_vy, 0);

  while (1)
  {
    // Measure time at start of thread
    begin_time = time_us_32();
    // erase boid
    fillCircle(fix2int15(boid0_x), fix2int15(boid0_y), 4, BLACK);
    // drawRect(fix2int15(boid0_x), fix2int15(boid0_y), 2, 2, BLACK);
    //  update boid's position and velocity
    wallsAndEdges(&boid0_x, &boid0_y, &boid0_vx, &boid0_vy);
    // draw the boid at its new position
    fillCircle(fix2int15(boid0_x), fix2int15(boid0_y), 4, ORANGE);
    // drawRect(fix2int15(boid0_x), fix2int15(boid0_y), 2, 2, color);
    //  draw the boundaries
    drawArena();
    calculateScore();
    // drawPaddle();
    //update_paddles_from_imus();
    // mpu6050_read_raw(acceleration, gyro);
    //  delay in accordance with frame rate
    spare_time = FRAME_RATE - (time_us_32() - begin_time);
    // yield for necessary amount of time
    PT_YIELD_usec(spare_time);
    // NEVER exit while
  } // END WHILE(1)
  PT_END(pt);
} // animation thread

// Animation on core 1
static PT_THREAD(protothread_anim1(struct pt *pt))
{
  // Mark beginning of thread
  PT_BEGIN(pt);

  // Variables for maintaining frame rate
  static int begin_time;
  static int spare_time;

  // Spawn a boid
  spawnBoid(&boid1_x, &boid1_y, &boid1_vx, &boid1_vy, 1);

  while (1)
  {
    // Measure time at start of thread
    begin_time = time_us_32();
    // erase boid
    drawRect(fix2int15(boid1_x), fix2int15(boid1_y), 2, 2, BLACK);
    // update boid's position and velocity
    wallsAndEdges(&boid1_x, &boid1_y, &boid1_vx, &boid1_vy);
    // draw the boid at its new position
    drawRect(fix2int15(boid1_x), fix2int15(boid1_y), 2, 2, color);
    // delay in accordance with frame rate
    spare_time = FRAME_RATE - (time_us_32() - begin_time);
    // yield for necessary amount of time
    PT_YIELD_usec(spare_time);
    // NEVER exit while
  } // END WHILE(1)
  PT_END(pt);
} // animation thread

// ========================================
// === core 1 main -- started in main below
// ========================================
void core1_main()
{
  // Add animation thread
  // pt_add_thread(protothread_anim1);
  // Start the scheduler
  pt_schedule_start;
}


// ========================================
// === main
// ========================================
// USE ONLY C-sdk library
int main()
{
  // initialize stio
  stdio_init_all();

  // initialize VGA
  initVGA();

  // Initialise both IMUs
  ////////////////////////////////////////////////////////////////////////
  ///////////////////////// I2C CONFIGURATION ////////////////////////////
  //i2c_init(I2C_CHAN, I2C_BAUD_RATE);
  //gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
  //gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);

  // Pullup resistors on breakout board, don't need to turn on internals
  // gpio_pull_up(SDA_PIN) ;
  // gpio_pull_up(SCL_PIN) ;

  // MPU6050 initialization
  //mpu6050_reset();
  //mpu6050_read_raw(accelA, gyroA);
  // start core 1
  multicore_reset_core1();
  multicore_launch_core1(&core1_main);

  // add threads
  pt_add_thread(protothread_serial);
  pt_add_thread(protothread_anim);

  // start scheduler
  pt_schedule_start;
}
