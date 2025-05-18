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

// Thickness of Paddle
#define PADDLE_THICK 8
#define PTH_HALF ((PADDLE_THICK - 1) / 2)
#define MAX_TILT_PX 10
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

// ----------  [NEW]  ----------  spin & Magnus
static fix15 ballSpin = 0;
static const fix15 spinDecay = float2fix15(0.995f);
static const fix15 magnusK = float2fix15(0.03f);

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

// DMA settings
//  Number of samples per period in sine table
#define sine_table_size 256
int data_chan;
int ctrl_chan;
// Sine table
int raw_sin[sine_table_size];

// Table of values to be sent to DAC
unsigned short DAC_data[sine_table_size];

// Pointer to the address of the DAC data table
unsigned short *address_pointer1 = &DAC_data[0];

// Global flag to track first-time start
bool is_first_start = true;
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000

// SPI configurations
#define PIN_MISO 4
#define PIN_CS 5
#define PIN_SCK 6
#define PIN_MOSI 7
#define SPI_PORT spi0

// Number of DMA transfers per event
const uint32_t transfer_count = sine_table_size;

// Live Paddle State (Shared with core-0 to draw)
volatile struct
{
  int xA, yA, xB, yB, vXA, vXB;
  int tiltA, tiltB;
} paddleState[2];

volatile int activeBuffer = 0;

// MPU addresses (tie AD0 low for 0x68, high for 0x69)
#define IMU_ADDR_A 0x68               // AD0 pin low
#define IMU_ADDR_B 0x69               // AD0 pin high
#define IMU_UPDATE_US 20000           // 50 Hz paddle refresh
#define XA_RANGE (250 - PADDLE_W)     // horiz. span for A
#define XB_MIN 420                    // B zone start
#define XB_RANGE (220 - PADDLE_W)     // horiz. span for B
#define Y_RANGE (SCREEN_H - PADDLE_H) // vertical span both
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

//--------------------------------------------------------------------
//  update_paddles_from_imus()  –  fixed-point, filtered, rate-limited
//--------------------------------------------------------------------
static fix15 filtA = 0; // LPF state (fix15 g-units)
static fix15 filtB = 0;
static const fix15 alpha = 2147; // 0.065 in Q15  (smaller ⇒ smoother)

static inline int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int mapAxisToRange(fix15 val, int range)
{ // val is -1…+1  g  in Q15
  return fix2int15(multfix15(val + float2fix15(1.0f),
                             int2fix15(range))) >>
         1;
}
static int last_xA = 0, last_yA = SCREEN_H / 2, last_xB = SCREEN_W - PADDLE_W, last_yB = SCREEN_H / 2;
// Extra state for the slanted paddles  ────────────────────────────────
static int last_tiltA = 0;
static int last_tiltB = 0;

static int last_xTopA = 0, last_yTopA = 0, last_xBotA = 0, last_yBotA = 0;
static int last_xTopB = 0, last_yTopB = 0, last_xBotB = 0, last_yBotB = 0;
static inline void draw_thick_line(int x1, int y1, int x2, int y2, uint16_t colour)
{
  for (int dx = -PTH_HALF; dx <= PTH_HALF; dx++)
  {
    drawLine(x1 + dx, y1, x2 + dx, y2, colour);
  }
}

static inline void erase_line(int x1, int y1, int x2, int y2)
{
  draw_thick_line(x1, y1, x2, y2, BLACK);
}

static inline void draw_line(int x1, int y1, int x2, int y2)
{
  draw_thick_line(x1, y1, x2, y2, RED);
}
static inline void draw_line2(int x1, int y1, int x2, int y2)
{
  draw_thick_line(x1, y1, x2, y2, BLUE);
}

static inline void draw_paddles_core0(void)
{
  /* --- PLAYER A --- */
  int xBase = paddleState[activeBuffer].xA, yBase = paddleState[activeBuffer].yA, tilt = paddleState[activeBuffer].tiltA;
  int xTop = xBase + tilt, yTop = yBase;
  int xBot = xBase - tilt, yBot = yBase + PADDLE_H;
  if (abs(xBase - last_xA) | abs(yBase - last_yA) | abs(tilt - last_tiltA))
  {
    erase_line(last_xTopA, last_yTopA, last_xBotA, last_yBotA);
    draw_line(xTop, yTop, xBot, yBot);
    last_xA = xBase;
    last_yA = yBase;
    last_tiltA = tilt;
    last_xTopA = xTop;
    last_yTopA = yTop;
    last_xBotA = xBot;
    last_yBotA = yBot;
  }
  /* --- PLAYER B --- */
  xBase = paddleState[activeBuffer].xB;
  yBase = paddleState[activeBuffer].yB;
  tilt = paddleState[activeBuffer].tiltB;
  xTop = xBase + tilt;
  yTop = yBase;
  xBot = xBase - tilt;
  yBot = yBase + PADDLE_H;
  if (abs(xBase - last_xB) | abs(yBase - last_yB) | abs(tilt - last_tiltB))
  {
    erase_line(last_xTopB, last_yTopB, last_xBotB, last_yBotB);
    draw_line2(xTop, yTop, xBot, yBot);
    last_xB = xBase;
    last_yB = yBase;
    last_tiltB = tilt;
    last_xTopB = xTop;
    last_yTopB = yTop;
    last_xBotB = xBot;
    last_yBotB = yBot;
  }
}
// ------------------------------------------------------------------
//                         ball reflection
// ------------------------------------------------------------------
static void reflectBallFromPaddle(fix15 *x, fix15 *y, fix15 *vx, fix15 *vy, int paddleX, int paddleY, int paddleVX)
{
  // Reset DMA channels before starting a new transfer
  dma_channel_abort(ctrl_chan);
  dma_channel_abort(data_chan);
  int bx = fix2int15(*x), by = fix2int15(*y);
  if (bx >= paddleX && bx <= paddleX + PADDLE_W &&
      by >= paddleY && by <= paddleY + PADDLE_H)
  {
    *vx = -*vx;

    int offset = by - (paddleY + PADDLE_H / 2);
    *vy += int2fix15(offset / 8);

    ballSpin = int2fix15(offset / 8) + int2fix15(paddleVX / 4);
    dma_start_channel_mask(1u << ctrl_chan); // Start both channels
    sleep_ms(10);                            // Allow sound to play for 100 ms
    dma_channel_abort(ctrl_chan);            // Stop the control channel
    dma_channel_abort(data_chan);            // Stop the data channel
    *vx = multfix15(*vx, float2fix15(1.07f));
    *vy = multfix15(*vy, float2fix15(1.05f));
  }
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
  // drawLine(100,50,200,20,WHITE);
  // drawLine(600,400,500,450,RED);
  //  drawVLine(540, 100, 280, WHITE) ;
  //  drawHLine(100, 100, 440, WHITE) ;
  //  drawHLine(100, 380, 440, WHITE) ;
}

void reset_board()
{
  fillRect(0, 0, 620, 480, BLACK);
}

void calculateScore()
{
  int diff1 = Score_A - Score_B;
  int diff2 = Score_B - Score_A;
  if (hitRight(boid0_x))
  {
    Score_A++;
    if ((Score_A == 11 && Score_B <= 9) || (Score_A > 11 && diff1 >= 1))
    {
      Score_A = 0;
      Score_B = 0;
      WinningScore_A++;
      dma_channel_abort(ctrl_chan);            // Stop the control channel
      dma_channel_abort(data_chan);            // Stop the data channel
      dma_start_channel_mask(1u << ctrl_chan); // Start both channels
      sleep_ms(10);                            // Allow sound to play for 100 ms
      dma_channel_abort(ctrl_chan);            // Stop the control channel
      dma_channel_abort(data_chan);            // Stop the data channel
      fillRect(fix2int15(boid0_x) - 4, fix2int15(boid0_y) - 4, 10, 10, BLACK);
      spawnBoid(&boid0_x, &boid0_y, &boid0_vx, &boid0_vy, 1);
      if (WinningScore_A == 3)
      {
        dma_channel_abort(ctrl_chan); // Stop the control channel
        dma_channel_abort(data_chan); // Stop the data channel
        WinningScore_B = 0;
        WinningScore_A = 0;
        // Do Nothing - Print A Wins and Reset the Board
        setTextColor(WHITE);
        setTextSize(1);
        setCursor(280, 240);
        writeString("Player A Wins");
        sleep_ms(2000);
        dma_start_channel_mask(1u << ctrl_chan); // Start both channels
        sleep_ms(30);                            // Allow sound to play for 100 ms
        dma_channel_abort(ctrl_chan);            // Stop the control channel
        dma_channel_abort(data_chan);            // Stop the data channel
        reset_board();
        fillRect(fix2int15(boid0_x) - 4, fix2int15(boid0_y) - 4, 8, 8, BLACK);
        spawnBoid(&boid0_x, &boid0_y, &boid0_vx, &boid0_vy, 1);
      }
    }
  }
  if (hitLeft(boid0_x))
  {
    Score_B++;
    if ((Score_B == 11 && Score_A <= 9) || (Score_B > 11 && diff2 >= 1))
    {
      Score_A = 0;
      Score_B = 0;
      dma_channel_abort(ctrl_chan);            // Stop the control channel
      dma_channel_abort(data_chan);            // Stop the data channel
      dma_start_channel_mask(1u << ctrl_chan); // Start both channels
      sleep_ms(10);                            // Allow sound to play for 100 ms
      dma_channel_abort(ctrl_chan);            // Stop the control channel
      dma_channel_abort(data_chan);            // Stop the data channel
      fillRect(fix2int15(boid0_x) - 4, fix2int15(boid0_y) - 4, 10, 10, BLACK);
      WinningScore_B++;
      spawnBoid(&boid0_x, &boid0_y, &boid0_vx, &boid0_vy, 0);
      if (WinningScore_B == 3)
      {
        dma_channel_abort(ctrl_chan); // Stop the control channel
        dma_channel_abort(data_chan); // Stop the data channel
        WinningScore_B = 0;
        WinningScore_A = 0;
        // Do Nothing - Print A Wins and Reset the Board
        setTextColor(WHITE);
        setTextSize(1);
        setCursor(280, 240);
        writeString("Player B Wins");
        sleep_ms(2000);
        reset_board();
        dma_start_channel_mask(1u << ctrl_chan); // Start both channels
        sleep_ms(30);                            // Allow sound to play for 100 ms
        dma_channel_abort(ctrl_chan);            // Stop the control channel
        dma_channel_abort(data_chan);            // Stop the data channel
        fillRect(fix2int15(boid0_x) - 4, fix2int15(boid0_y) - 4, 10, 10, BLACK);
        spawnBoid(&boid0_x, &boid0_y, &boid0_vx, &boid0_vy, 0);
      }
    }
  }
}

// call once at startup (before you launch cores/threads)
void init_two_mpus()
{
  // initialize I2C0 at 400 kHz on GP4=SDA, GP5=SCL, GP2 and GP3
  // since we are using I2C on channel 1 and
  // I2C0 is being used by vga;
  i2c_init(i2c1, 400 * 1000);
  gpio_set_function(2, GPIO_FUNC_I2C);
  gpio_set_function(3, GPIO_FUNC_I2C);
  // No need to turn pull up since they are on breakout board that we are using
  // gpio_pull_up(4); gpio_pull_up(5);
  mpu6050_reset(IMU_ADDR_A);
  mpu6050_reset(IMU_ADDR_B);
}

// Detect wallstrikes, update velocity and position
void wallsAndEdges(fix15 *x, fix15 *y, fix15 *vx, fix15 *vy)
{
  // ----------  [MOD]  ----------  Magnus from spin
  *vy += multfix15(ballSpin, magnusK);
  ballSpin = multfix15(ballSpin, spinDecay);
  // ---------- [END]  ----------
  if (hitTop(*y) || hitBottom(*y) || hitRight(*x) || hitLeft(*x))
  {
    // Reset DMA channels before starting a new transfer
    dma_channel_abort(ctrl_chan);
    dma_channel_abort(data_chan);
    printf("Starting DMA transfer...\n");
    dma_start_channel_mask(1u << ctrl_chan); // Start both channels
    sleep_ms(10);                            // Allow sound to play for 100 ms
    dma_channel_abort(ctrl_chan);            // Stop the control channel
    dma_channel_abort(data_chan);            // Stop the data channel
    printf("DMA transfer completed.\n");
  }
  if (hitTop(*y))
  {
    *vy = -*vy;
    *y += int2fix15(5);
  }
  if (hitBottom(*y))
  {
    *vy = -*vy;
    *y -= int2fix15(5);
  }
  if (hitRight(*x))
  {
    *vx = -*vx;
    *x -= int2fix15(5);
  }
  if (hitLeft(*x))
  {
    *vx = -*vx;
    *x += int2fix15(5);
  }

  *x += *vx;
  *y += *vy;
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
    // Check if the reset button (GPIO 15) is pressed
    // Check if the reset button (GPIO 15) is pressed
    if (!gpio_get(15)) // Button is active low
    {
      // Reset game state
      fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK); // Clear the screen
      Score_A = 0;
      Score_B = 0;
      WinningScore_A = 0;
      WinningScore_B = 0;
      lastY_A = (SCREEN_H - PADDLE_H) / 2;
      lastY_B = (SCREEN_H - PADDLE_H) / 2;
      spawnBoid(&boid0_x, &boid0_y, &boid0_vx, &boid0_vy, 0);
      reset_board();

      // Display reset message only if not the first start
      if (!is_first_start)
      {
        setTextColor(WHITE);
        setTextSize(2);
        setCursor(200, 200);
        writeString("Game Reset!");
        sleep_ms(2000);                            // Pause for 2 seconds
        fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK); // Clear the screen
      }

      // Set the flag to false after the first start
      is_first_start = false;
    }
    // erase boid
    fillCircle(fix2int15(boid0_x), fix2int15(boid0_y), 4, BLACK);
    // update boid's position and velocity
    wallsAndEdges(&boid0_x, &boid0_y, &boid0_vx, &boid0_vy);
    // draw the boid at its new position
    fillCircle(fix2int15(boid0_x), fix2int15(boid0_y), 4, ORANGE);
    // draw the boundaries and calcultate the score
    drawArena();
    calculateScore();
    draw_paddles_core0();
    int readBuffer = activeBuffer;
    reflectBallFromPaddle(&boid0_x, &boid0_y, &boid0_vx, &boid0_vy, paddleState[readBuffer].xA, paddleState[readBuffer].yA, paddleState[readBuffer].vXA);
    reflectBallFromPaddle(&boid0_x, &boid0_y, &boid0_vx, &boid0_vy, paddleState[readBuffer].xB, paddleState[readBuffer].yB, paddleState[readBuffer].vXB);
    // delay in accordance with frame rate
    spare_time = FRAME_RATE - (time_us_32() - begin_time);
    // yield for necessary amount of time
    PT_YIELD_usec(spare_time);
    // NEVER exit while
  } // END WHILE(1)
  PT_END(pt);
} // Animation thread

// Animation on core 1
static PT_THREAD(protothread_imus(struct pt *pt))
{
  // ----------  [NEW]  ----------
  static fix15 filtAX = 0, filtAY = 0, filtAZ = 0;
  static fix15 filtBX = 0, filtBY = 0, filtBZ = 0;
  const fix15 alpha = float2fix15(0.025f); // Smoothing factor for LPF
  const float gyro_coeff = 0.08f;          // deg/s → g-eq
  static int lastxA = 0, lastxB = 0;
  PT_BEGIN(pt);

  while (1)
  {
    fix15 aA[3], gA[3], aB[3], gB[3];
    mpu6050_reset(IMU_ADDR_A);
    mpu6050_reset(IMU_ADDR_B);
    mpu6050_read_raw(IMU_ADDR_A, aA, gA);
    mpu6050_read_raw(IMU_ADDR_B, aB, gB);

    // Apply low-pass filter (LPF) to smooth IMU readings
    filtAX = filtAX + multfix15(alpha, (aA[0] << 1) - filtAX);
    filtAY = filtAY + multfix15(alpha, (aA[1] << 1) - filtAY);
    filtAZ = filtAZ + multfix15(alpha, (aA[2] << 1) - filtAZ);

    filtBX = filtBX + multfix15(alpha, (aB[0] << 1) - filtBX);
    filtBY = filtBY + multfix15(alpha, (aB[1] << 1) - filtBY);
    filtBZ = filtBZ + multfix15(alpha, (aB[2] << 1) - filtBZ);

    // ------------------ PLAYER A (left) ------------------
    int xA = mapAxisToRange(-filtAZ, XA_RANGE); // Horizontal
    int yA = mapAxisToRange(filtAX, Y_RANGE);   // Vertical
    int tiltA = -fix2int15(multfix15(filtAZ, int2fix15(MAX_TILT_PX)));

    // Clamp Player A's paddle position to its range
    xA = clamp(xA, 0, XA_RANGE);
    yA = clamp(yA, 0, SCREEN_H - PADDLE_H);

    // ------------------ PLAYER B (right) ------------------
    int xB = XB_MIN + mapAxisToRange(filtBZ, XB_RANGE); // Horizontal
    int yB = mapAxisToRange(filtBX, Y_RANGE);           // Vertical
    int tiltB = fix2int15(multfix15(filtBZ, int2fix15(MAX_TILT_PX)));
    // Clamp Player B's paddle position to its range
    xB = clamp(xB, XB_MIN, XB_MIN + XB_RANGE);
    yB = clamp(yB, 0, SCREEN_H - PADDLE_H);

    // Update paddle state for both players
    int writeBuffer = 1 - activeBuffer;
    paddleState[writeBuffer].vXA = xA - lastxA;
    paddleState[writeBuffer].vXB = xB - lastxB;
    paddleState[writeBuffer].xA = xA;
    paddleState[writeBuffer].yA = yA;
    paddleState[writeBuffer].tiltA = tiltA;
    paddleState[writeBuffer].xB = xB;
    paddleState[writeBuffer].yB = yB;
    paddleState[writeBuffer].tiltB = tiltB;
    activeBuffer = writeBuffer;

    lastxA = xA;
    lastxB = xB;

    PT_YIELD_usec(IMU_UPDATE_US); // Update at 50 Hz
  }
  PT_END(pt);
}

// ========================================
// === core 1 main -- started in main below
// ========================================
void core1_main()
{
  // Add animation thread
  pt_add_thread(protothread_imus);
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

  // Initialize the button (e.g., GPIO15)
  const uint BUTTON_PIN = 15;
  gpio_init(BUTTON_PIN);
  gpio_set_dir(BUTTON_PIN, GPIO_IN);
  gpio_pull_up(BUTTON_PIN); // Enable internal pull-up resistor

  // Display the welcome message
  fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK); // Clear the screen
  setTextColor(BLUE);
  setTextSize(6);
  setCursor(200, 50);
  writeString("Welcome");
  setCursor(280, 150);
  writeString("To");
  setCursor(80, 250);
  writeString("Table - Tennis");
  setTextColor(WHITE);
  setTextSize(1);
  setCursor(40, 350);
  writeString("Features and Rules:");
  setCursor(40, 360);
  writeString("1. Player A is on Left and Player B is on Right");
  setCursor(40, 370);
  writeString("2. Player A uses Left IMU and Player B uses Right IMU");
  setCursor(40, 380);
  writeString("3. The paddle is slanted and can be tilted to hit the ball");
  setCursor(40, 390);
  writeString("4. The ball can spin and curve in the air");
  setCursor(40, 400);
  writeString("5. The ball can bounce off the paddle and walls");
  setCursor(40, 410);
  writeString("6. Upon hitting, You can hear a sound");
  setCursor(40, 420);
  writeString("7. The game is played to 11 points, win by difference of 2");
  setCursor(40, 430);
  writeString("8. Whoever wins 3 sets wins the game");
  setTextColor(RED);
  setTextSize(1);
  setCursor(500, 460);
  writeString("Press button to start");

  // Wait for button press
  while (gpio_get(BUTTON_PIN))
  {
    sleep_ms(100); // Poll every 100ms
  }
  // Clear the screen after button press
  fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);

  // Initialise TWO IMUs
  init_two_mpus();

  // Initialize SPI channel (channel, baud rate set to 20MHz)
  spi_init(SPI_PORT, 20000000);

  // Format SPI channel (channel, data bits per transfer, polarity, phase, order)
  spi_set_format(SPI_PORT, 16, 0, 0, 0);

  // Map SPI signals to GPIO ports
  gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_CS, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

  // Build sine table and DAC data table
  for (int i = 0; i < sine_table_size; i++)
  {
    raw_sin[i] = (int)(2047 * sin((float)i * 6.283 / (float)sine_table_size) + 2047); // 12-bit
    DAC_data[i] = DAC_config_chan_A | (raw_sin[i] & 0x0fff);
  }

  // Configure DMA timer 0
  dma_hw->timer[0] = (1u << 16) | (125u); // Timer frequency: 125 cycles per tick

  // Select DMA channels
  data_chan = dma_claim_unused_channel(true);
  ctrl_chan = dma_claim_unused_channel(true);

  // Setup the control channel
  dma_channel_config c = dma_channel_get_default_config(ctrl_chan);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  channel_config_set_read_increment(&c, false);
  channel_config_set_write_increment(&c, false);
  channel_config_set_chain_to(&c, data_chan);

  dma_channel_configure(
      ctrl_chan,
      &c,
      &dma_hw->ch[data_chan].read_addr,
      &address_pointer1,
      1,
      false);

  // Setup the data channel
  dma_channel_config c2 = dma_channel_get_default_config(data_chan);
  channel_config_set_transfer_data_size(&c2, DMA_SIZE_16);
  channel_config_set_read_increment(&c2, true);
  channel_config_set_write_increment(&c2, false);
  dma_timer_set_fraction(0, 0x0017, 0xffff);
  channel_config_set_dreq(&c2, 0x3b); // DREQ paced by timer 0
  channel_config_set_chain_to(&c2, ctrl_chan);

  dma_channel_configure(
      data_chan,
      &c2,
      &spi_get_hw(SPI_PORT)->dr,
      DAC_data,
      sine_table_size,
      false);

  multicore_reset_core1();
  multicore_launch_core1(&core1_main);

  // add threads
  // pt_add_thread(protothread_serial);
  // You can modify these to take number of balls, alpha and other parameters as input to make it more interactive
  pt_add_thread(protothread_anim);

  // start scheduler
  pt_schedule_start;
}