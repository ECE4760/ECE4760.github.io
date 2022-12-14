/**
 * Hunter Adams (vha3@cornell.edu)
 * 
 * This demonstration animates two balls bouncing about the screen.
 * Through a serial interface, the user can change the ball color.
 *
 * HARDWARE CONNECTIONS
 *  - GPIO 16 ---> VGA Hsync
 *  - GPIO 17 ---> VGA Vsync
 *  - GPIO 18 ---> 330 ohm resistor ---> VGA Red
 *  - GPIO 19 ---> 330 ohm resistor ---> VGA Green
 *  - GPIO 20 ---> 330 ohm resistor ---> VGA Blue
 *  - RP2040 GND ---> VGA GND
 *
 * RESOURCES USED
 *  - PIO state machines 0, 1, and 2 on PIO instance 0
 *  - DMA channels 0, 1
 *  - 153.6 kBytes of RAM (for pixel color data)
 *
 */

// Include the VGA grahics library
#include "vga_graphics.h"
#include "Audio.h" //Include the snake audio library
// Include standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
// Include Pico libraries
#include "pico/stdlib.h"
#include "pico/divider.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "hardware/spi.h"
// Include hardware libraries
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
// Include protothreads
#include "pt_cornell_rp2040_v1.h"

// === the fixed point macros ========================================
typedef signed int fix15 ;
#define multfix15(a,b) ((fix15)((((signed long long)(a))*((signed long long)(b)))>>15))
#define float2fix15(a) ((fix15)((a)*32768.0)) // 2^15
#define fix2float15(a) ((float)(a)/32768.0)
#define absfix15(a) abs(a) 
#define int2fix15(a) ((fix15)(a << 15))
#define fix2int15(a) ((int)(a >> 15))
#define char2fix15(a) (fix15)(((fix15)(a)) << 15)
#define divfix(a,b) (fix15)(div_s64s64( (((signed long long)(a)) << 15), ((signed long long)(b))))

//Audio pins
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7

//Buttons
#define RIGHT 8
#define LEFT 9
#define UP 10
#define DOWN 11

//Audio and ADC defines
// #define PARAM1 ADC_FORMAT_INTG16 | ADC_CLK_AUTO | ADC_AUTO_SAMPLING_OFF
// #define PARAM2 ADC_VREF_AVDD_AVSS | ADC_OFFSET_CAL_DISABLE | ADC_SCAN_OFF | ADC_SAMPLES_PER_INT_1 | ADC_ALT_BUF_OFF | ADC_ALT_INPUT_OFF
// #define PARAM3 ADC_CONV_CLK_PB | ADC_SAMPLE_TIME_5 | ADC_CONV_CLK_Tcy2
// #define PARAM4 ENABLE_AN11_ANA
// #define PARAM5 SKIP_SCAN_ALL

#define CHOMP_AUDIO_SIZE 49546  //size is equivalent to 2x the number of elements in the array
#define DEATH_AUDIO_SIZE 18618
// #define DMA_CHANNEL2 2
// #define DMA_CHANNEL3 3
#define SPI_CLK_DIV 2

// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000
#define SPI_PORT spi0

const unsigned short DAC_output_chomp[];
const unsigned short DAC_output_death[];
// SPI data
uint16_t DAC_data_1; // output value
uint16_t DAC_data_0; // output value

// Sine table for the FFT calculation
fix15 Sinewave[CHOMP_AUDIO_SIZE];
// Hann window table for FFT calculation
fix15 window[CHOMP_AUDIO_SIZE];

// DDS sine table (populated in main())
#define sine_table_size 256
fix15 sin_table[sine_table_size];

// Semaphore
struct pt_sem core_1_go, core_0_go;

// Two variables to store core number
volatile int corenum_0;
volatile int corenum_1;
//FROM LAB 1:
// DMA channels for sampling ADC (VGA driver uses 0 and 1)
int DMA_CHANNEL2CHOMP = 2;
int DMA_CHANNEL3DEATH = 3;

//LAB 1 USED PINS 7-10, check what to wire the DAC (MCP4822 chip) to
//In LAB 1, pin 8 corresponded to SCK and pin 9 to SDI. I don't think we need to connect the LDAC (blue wire in the lab 1 report schematic)



// uS per frame
#define FRAME_RATE 33000
#define num_boids_global 50
#define visualRange int2fix15(40)
#define protectedRange int2fix15(8)
#define avoidfactor float2fix15(.05)
#define centering_factor float2fix15(.0005)
#define matching_factor float2fix15(.05)
#define bias_increment float2fix15(.0004)
#define biasval float2fix15(.001)

// turnfactor: 0.2
// visualRange: 40
// protectedRange: 8
// centeringfactor: 0.0005
// avoidfactor: 0.05
// matchingfactor: 0.05
// maxspeed: 6
// minspeed: 3
// maxbias: 0.01
// bias_increment: 0.00004
// default biasval: 0.001 (user-changeable, or updated dynamically)
// #define  turnFactor unsigned short _Fract(.2)
#define TURNING_FACTOR float2fix15(.4) // TODO: change back to 0.2
#define MAX_SPEED int2fix15(6)
#define MIN_SPEED int2fix15(3)

#define Top 100   //140
#define Bottom 500
#define Left 100  //140
#define Right 500 //540

//defines number of grids per side (default 20)
#define grid_size 20
#define square_size (Bottom-Top)/grid_size

//wall detection
#define hitBottom(b) (b>=(grid_size-1))
#define hitTop(b) (b<0)
#define hitLeft(a) (a<0)
#define hitRight(a) (a>=grid_size)

#define hitApple(a) (a>=grid_size)


#define snake_length_max 400

int WrapTop = 0;
int WrapBottom = 479;
int WrapLeft = 0;
int WrapRight = 639;

int Center = 320;
int Center_y = 240;

enum border_type {Box, Free, Column};
enum border_type current_border;

int custom_width = 100;

//checked during while loop; on game end, indicate to user
bool game_over = false;
bool hard_mode = false;
int snake_height;
int snake_width;


// the color of the boid
char color = WHITE ;


// Boid on core 0
fix15 boid0_x ;
fix15 boid0_y ;
fix15 boid0_vx ;
fix15 boid0_vy ;



// Boid on core 1
fix15 boid1_x ;
fix15 boid1_y ;
fix15 boid1_vx ;
fix15 boid1_vy ;

typedef struct Snake {
  short x;
  short y;
  bool goingUp;
  bool goingDown;
  bool goingRight;
  bool goingLeft;
  bool isValid;
  struct Snake *leading;
  struct Snake *trailing;
} snake_t;

typedef struct Apple {
  short x;
  short y;
} apple_t;

typedef struct Block {
  short x;
  short y;
} block_t;



// Create a boid

void spawnHead(snake_t *snake)
{
  // Start in center of screen
  snake->x = (short)13;
  snake->y = (short)3;
  // Choose left or right
  snake->goingUp = false;
  snake->goingDown = true;
  snake->goingLeft = false;
  snake->goingRight = false;

  snake->isValid = true;

  snake->leading = NULL;
  snake->trailing = NULL;
}

int points = 0;
int high_score = 0;

//check if snake head eats/collides with the apple
bool space_not_taken(short x, short y, snake_t *segment) {
  if(x == segment->x && y == segment->y) {
    return false;
  }
  if(segment->trailing!=NULL) {
    return space_not_taken(x,y,segment->trailing);
  }
  return true;
}

void generateNewApple(apple_t *apple, snake_t* snake_head)
{
  do {
  // Start in center of screen
  short init_x = (rand()%grid_size);
  short init_y = (rand()%(grid_size-1));
  // short init_y = 0;
  // int init_x = 340;
  // int init_y = 240;
  apple->x = (short)init_x; //init_x;
  apple->y = (short)init_y;
  } while(!space_not_taken(apple->x,apple->y,snake_head));
}

void generateBlock(block_t *block, snake_t* snake_head)
{
  do {
  // Start in center of screen
  short init_x = (rand()%grid_size);
  short init_y = (rand()%(grid_size-1));
  // short init_y = 0;
  // int init_x = 340;
  // int init_y = 240;
  block->x = (short)init_x; //init_x;
  block->y = (short)init_y;
  } while(!space_not_taken(block->x,block->y,snake_head));
}


block_t* addBlock(block_t** block_tail_ptr, snake_t* head){
  block_t* old_tail = *block_tail_ptr;
  block_t* new_tail = old_tail+1;
  do {
  short init_x = (rand()%grid_size);
  short init_y = (rand()%(grid_size-1));
  new_tail->x = (short)init_x;
  new_tail->y = (short)init_y;
  } while(!space_not_taken(new_tail->x,new_tail->y,head));
  return new_tail;
}

// Draw the boundaries
void drawArenaBox() {
  //was originally cyan
  drawVLine(Left, Top, Bottom-Top, RED) ; //(x,y,length down/right)
  drawVLine(Right, Top, Bottom-Top, RED) ;
  drawHLine(Left, Top, Right-Left, RED) ;
  drawHLine(Left, Bottom, Right-Left, RED) ;
}

// Detect wallstrikes, update velocity and position
void wallsAndEdges(snake_t *head)
{
  if(hitTop(head->y) || hitRight(head->x) || hitBottom(head->y) || hitLeft(head->x)){
    game_over = true;
  }
}

void updatePosition(fix15* x, fix15* y, fix15* vx, fix15* vy){
  // Update position using velocity
  *x = *x + int2fix15(snake_width);
  *y = *y + int2fix15(snake_height);
}

// TODO: implement setDirection functions that change booleans in snake_head
void setUp(snake_t *snake) {
  if (!(snake->goingDown)){
  snake->goingUp = true;
  snake->goingDown = false;
  snake->goingLeft = false;
  snake->goingRight = false;
  }
}
void setDown(snake_t *snake) {
  if (!(snake->goingUp)){
  snake->goingUp = false;
  snake->goingDown = true;
  snake->goingLeft = false;
  snake->goingRight = false;
  }
}
void setLeft(snake_t *snake) {
  if (!(snake->goingRight)){
  snake->goingUp = false;
  snake->goingDown = false;
  snake->goingLeft = true;
  snake->goingRight = false;
  }
}
void setRight(snake_t *snake) {
  if (!(snake->goingLeft)){
  snake->goingUp = false;
  snake->goingDown = false;
  snake->goingLeft = false;
  snake->goingRight = true;
  }
}
// TODO: implement functions for moving the snake head, making segments follow
void moveInCurrentDirection(snake_t *snake) {
  if (snake->goingUp){
    snake->y= (snake->y) - 1 ;
    //snake->y--;
  }
  if (snake->goingDown){
    snake->y= (snake->y) + 1 ;
    //snake->y++;
  }
  if (snake->goingRight){
    snake->x= (snake->x) + 1 ;
    //snake->x++;
  }
  if (snake->goingLeft){
    snake->x= (snake->x) - 1 ;
    //snake->x--;
  }
}
void recursiveFollow(snake_t *snake)
{
  if (snake->leading != NULL)
  {
    snake->x = snake->leading->x;
    snake->y = snake->leading->y;
    snake->goingUp = snake->leading->goingUp;
    snake->goingDown = snake->leading->goingDown;
    snake->goingRight = snake->leading->goingRight;
    snake->goingLeft = snake->leading->goingLeft;
    recursiveFollow(snake->leading);
  }
}

//implement functions to check collision, delete apples, add new tails to snake
bool collided(snake_t *snake, apple_t apple) {}
void deleteApple(apple_t *apple) {
  drawRect(fix2int15(apple->x), fix2int15(apple->y), 16, 16, BLACK);            //snake_width/height for the apple too?
  drawRect(fix2int15(apple->x)+14, fix2int15(apple->y)-4, 4, 4, BLACK);
}

bool game_start = false;

// // TODO: implement color changing, color for game, etc.
// void color_change(snake_t snake_head) {
//   snake_t* segment = snake_head;
//   do {
//     // drawSnake(segment,)
//     // change color of specific segment
//   } while(segment->trailing != NULL)
// }

//function to end the game
void end_game() {
  fillRect(200, 180 , 205, 150, BLUE);
  setCursor(250, 200) ;
  setTextSize(2) ;
  setTextColor(RED);
  writeString("GAME OVER!") ;
  //high score
  char high_text[80];
  setTextColor(WHITE) ;
  sprintf(high_text, "%d", high_score);   //make incrementing "score" variable for every apple you eat
  setCursor(225, 250) ;
  setTextSize(2) ;
  writeString("High Score: ") ;
  writeString(high_text);
  //replay button
  setCursor(225, 300) ;
  setTextSize(1) ;
  setTextColor(WHITE);
  writeString("PRESS ANY BUTTON TO REPLAY!") ;
  
  game_start=false;
}

void start_game() {
  setCursor(150, 250) ;
  setTextSize(2) ;
  setTextColor(WHITE);
  writeString("PRESS ANY BUTTON TO PLAY!") ;
}


// TODO: implement functions for drawing snake, drawing apple, updating score
void updateScoreDisplay(int game_score) {
  char score_text[80];
  setTextColor(WHITE) ;
  fillRect(70, 0 , 75, 25, BLACK);
  sprintf(score_text, "%d", game_score);   //make incrementing "score" variable for every apple you eat
  setCursor(10, 10) ;
  setTextSize(2) ;
  writeString("Score:") ;
  writeString(score_text);
}
void drawSnake(snake_t *head, snake_t *tail, bool erase) {
  if(erase && (tail->isValid) == true) {
    //fillRect(Left+((tail->x)*square_size), Top+((tail->y)*square_size), square_size, square_size, BLACK);
    if(hard_mode==true){
      fillRect(Left+((tail->x)*square_size), Top+((tail->y)*square_size), square_size, square_size, BLUE);
    }
    else{
      if (((tail->x)+(tail->y))%2 == 0){ fillRect(Left+((tail->x)*square_size), Top+((tail->y)*square_size), square_size, square_size, CYAN); }
      else {fillRect(Left+((tail->x)*square_size), Top+((tail->y)*square_size), square_size, square_size, BLUE); }
    }
  } else if((head->isValid) == true) {
    fillRect(Left+((head->x)*square_size), Top+((head->y)*square_size), square_size, square_size, GREEN);
    drawRect(Left+((head->x)*square_size), Top+((head->y)*square_size), square_size, square_size, YELLOW);
  }
  // drawRect((short)250, (short)250, 16, 16, GREEN);
}
void drawApple(apple_t *apple, bool erase) {
  if(erase) {
    //fillRect(Left+((apple->x)*square_size), Top+((apple->y)*square_size), square_size, square_size, BLACK); 
    if (((apple->x)+(apple->y))%2 == 0){ fillRect(Left+((apple->x)*square_size), Top+((apple->y)*square_size), square_size, square_size, CYAN); }
      else {fillRect(Left+((apple->x)*square_size), Top+((apple->y)*square_size), square_size, square_size, BLUE); }
  } else {
      fillRect(Left+((apple->x)*square_size), Top+((apple->y)*square_size), square_size, square_size, RED); 
    drawRect(Left+((apple->x)*square_size), Top+((apple->y)*square_size), square_size, square_size, BLACK);
  }
}
void drawBlock(block_t *block) {
      fillRect(Left+((block->x)*square_size), Top+((block->y)*square_size), square_size, square_size, BLACK); 
}


void printInfo(int num_boids, float fps, int runtime) {
  setCursor(250, 40) ;
  setTextSize(2) ;
  setTextColor(WHITE);
  writeString("Snake Game");
}

void displayHard(){
  fillRect(290, 65 , 60, 20, BLACK);
  if(hard_mode==true){
    setCursor(290, 65) ;
    setTextSize(2) ;
    setTextColor(RED);
    writeString("HARD");
  }
}

// Variables for maintaining frame rate
bool up_button_pressed = false;
bool down_button_pressed = false;
bool left_button_pressed = false;
bool right_button_pressed = false;

apple_t apple_objects[1];
// generateNewApple(apple);
block_t block_objects[40];

// TODO: 10000 is an arbitrary number, find actual cycles per frame later
int cycles_per_frame = 600; //60;            //TRY UPDATING SNAKE EVERY 10 FRAMES

int count = 0;
snake_t snake_array[snake_length_max];//TODO: make so that instead of 24 is just max number of snake segments

snake_t* addTail(snake_t** snake_tail_ptr){
  snake_t* old_tail = *snake_tail_ptr;
  snake_t* new_tail = old_tail+1;
  if (old_tail < &snake_array[snake_length_max-1]){
    old_tail->trailing = new_tail;
    new_tail->leading = old_tail;
    new_tail->trailing = NULL;
    if (old_tail->goingUp){
      new_tail->x = old_tail->x;
      new_tail->y = old_tail->y+1;
    }
    if (old_tail->goingDown){
        new_tail->x = old_tail->x;
        new_tail->y = old_tail->y-1;
      }
    if (old_tail->goingRight){
        new_tail->x = old_tail->x-1;
        new_tail->y = old_tail->y;
      }
    if (old_tail->goingLeft){
        new_tail->x = old_tail->x+1;
        new_tail->y = old_tail->y;
      }
    new_tail->goingUp = old_tail->goingUp;
    new_tail->goingDown = old_tail->goingDown;
    new_tail->goingRight = old_tail->goingRight;
    new_tail->goingLeft = old_tail->goingLeft;
    new_tail->isValid = true;
  //make sure new grid position is set to a valid position
  }
  return new_tail;
}


void gameStart(){ //snake_t *head, snake_t* tail, apple_t* apple
  while(!game_start) {
      if(gpio_get(UP)) {//up_button_pressed
       // sprintf(pt_serial_out_buffer, "UP\n");
        game_over = false;
        //reset_game();
        game_start = true;
        //set hard flag to true and make the arena board ONLY blue AND add blocks
        hard_mode = true;
        displayHard();
        
      }
      if(gpio_get(DOWN)) { //down_button_pressed
        game_over = false;
        //reset_game();
        game_start = true;
        hard_mode = false;
        displayHard();
      }
      if(gpio_get(LEFT)) { //left_button_pressed
        game_over = false;
        //reset_game();
        game_start = true;
        hard_mode = false;
        displayHard();

      }
      if(gpio_get(RIGHT) ) { //right_button_pressed
        game_over = false;
        //reset_game();
        game_start = true;
        hard_mode = false;
        displayHard();
      }
    }
    if(game_start){
       //fillRect(150, 150 , 250, 200, BLACK);
       points = 0;
       updateScoreDisplay(points);
    }
}

// This timer ISR is called on core 1
bool repeating_timer_callback_core_1(struct repeating_timer *t)
{
        //if (STATE_1 == 0)  //IF CHOMP FLAG == TRUE      (THEN SET FLAG BACK TO FALSE AFTER SOUND IS PLAYED)
        //{ //chirp state
            // DDS phase and sine table lookup
            //phase_accum_main_1 += phase_incr_main_1;
            //DAC_output_1 = fix2int15(multfix15(current_amplitude_1,sin_table[phase_accum_main_1 >> 24])) + 2048;

            // Ramp up amplitude
            //if (count_1 < ATTACK_TIME)
            //{
            //    current_amplitude_1 = (current_amplitude_1 + attack_inc);
            //}
            // Ramp down amplitude
           // else if (count_1 > SYLLABLE_DURATION - DECAY_TIME)
           // {
           //     current_amplitude_1 = (current_amplitude_1 - decay_inc);
            //}

            // Mask with DAC control bits
            //DAC_data_1 = (DAC_config_chan_A | (DAC_output_1 & 0xffff));

            // SPI write (no spinlock b/c of SPI buffer)
            //spi_write16_blocking(SPI_PORT, &DAC_data_1, 1);
        //}
    

    // retrieve core number of execution
    corenum_1 = get_core_num();

    return true;
}


int snake_height = ((540-100)/20);
int snake_width = ((540-100)/20);

int temp = 0;
// Animation on core 0
static PT_THREAD (protothread_anim(struct pt *pt))
{
    // Mark beginning of thread
    PT_BEGIN(pt);
    //DAC_output_1 = fix2int15(multfix15(1, chompSound)) + 2048;
    // DAC_output_chomp = chompSound;
    // DAC_output_death = deathSound;

    // // Mask with DAC control bits
    // DAC_data_chomp = (DAC_config_chan_A | (DAC_output_chomp & 0xffff));
    // DAC_data_death = (DAC_config_chan_A | (DAC_output_death & 0xffff));

   
    if(!game_over){
      if(hard_mode==true){ fillRect(Left, Top, 20*square_size, 20*square_size, BLUE);
        }
      else{
      for (int h=0; h < 20; h++){
        for (int v=0; v <20; v++){

      //OR option 2: draw over with a colored checkerboard pattern:
            if ((h+v)%2 == 0){ fillRect(Left+(h*square_size), Top+(v*square_size), square_size, square_size, CYAN); }
            else {fillRect(Left+(h*square_size), Top+(v*square_size), square_size, square_size, BLUE); }
          }
            }
      }
      fillRect(120, 240 , 340, 40, BLUE);
      drawArenaBox();
      start_game();
      printInfo(0,30.0,time_us_32());
    }
    gameStart();
    while(game_start) {
      // while(game_over){
        //we believe this doesnt do anything
      //   gameStart();
      //     }

      
      fillRect(Left-square_size,Top-square_size,square_size*22,square_size*22,BLACK);
      if(hard_mode==true){ fillRect(Left, Top, 20*square_size, 20*square_size, BLUE);
        }
      else{
        for (int h=0; h < 20; h++){
        for (int v=0; v <20; v++){
            //draw over with a colored checkerboard pattern:
            if ((h+v)%2 == 0){ fillRect(Left+(h*square_size), Top+(v*square_size), square_size, square_size, CYAN); }
              else {fillRect(Left+(h*square_size), Top+(v*square_size), square_size, square_size, BLUE); }
            if(v==0){fillRect(Left+(h*square_size), Top-square_size, square_size, square_size, BLACK);}

            if(h==0){ fillRect(Left-square_size, Top+(v*square_size), square_size, square_size, BLACK);}
            if(h==20){ fillRect(Right, Top+(v*square_size), square_size, square_size, BLACK);}
            }
          
          }
      }
        
      drawArenaBox();
      points = 0;
      updateScoreDisplay(points);
      static snake_t* snake_head = &snake_array[0];
      static snake_t* snake_tail = &snake_array[0];
      static snake_t* prev_tail = &snake_array[0];
      static apple_t* game_apple = &apple_objects[0];
      static block_t* block_head = &block_objects[0];
      static block_t* block_tail = &block_objects[0];
      (&block_objects[0])->x = (short)-2;
      (&block_objects[0])->y = (short)-2;
      block_t* block_tail_org = block_tail;

      spawnHead(snake_head);

      if (temp==0){
        snake_tail = addTail(&snake_tail);
        drawSnake(snake_tail, snake_tail, false);
        snake_tail = addTail(&snake_tail);
        drawSnake(snake_tail, snake_tail, false);
        temp++;
      }

      generateNewApple(game_apple, snake_head);
      updateScoreDisplay(points);
      printInfo(0,30.0,time_us_32());
      gameStart();
      // snake_head->x = (short)250;
      // setting width/height of each segment
      //game_over = false;
      
    // gameStart(snake_head,snake_tail,game_apple);

    while(!game_over) {
      static int begin_time ;
      static int spare_time ;

      // Measure time at start of thread

      // non-blocking write
      // serial_write ;

      // draw game box
      begin_time = time_us_32() ; 
      // used to keep track of frames
      count++;
      // non-blocking write
      if(gpio_get(UP)) {//up_button_pressed
       // sprintf(pt_serial_out_buffer, "UP\n");
        setUp(snake_head);
        // depending on button pressed, sets direction for snake to move once game continues
      }
      if(gpio_get(DOWN)) { //down_button_pressed
        //sprintf(pt_serial_out_buffer, "DOWN\n");
        setDown(snake_head);
      }
      if(gpio_get(LEFT)) { //left_button_pressed
        //sprintf(pt_serial_out_buffer, "LEFT\n");
        setLeft(snake_head);
      }
      if(gpio_get(RIGHT) ) { //right_button_pressed
        //sprintf(pt_serial_out_buffer, "RIGHT\n");
        setRight(snake_head);
      }
      
      if(count > cycles_per_frame && game_start) {
        // once per frame
        count = 0;
        // erase old tail, move head, only need to do that to simulate movement
        
        drawApple(game_apple, true);
        drawSnake(snake_head, snake_tail, true);
        recursiveFollow(snake_tail);
        moveInCurrentDirection(snake_head);

        //Draw apple and draw new head location
        drawApple(game_apple, false);
        drawSnake(snake_head, snake_tail, false);                 
        drawArenaBox() ;
      }
      if(game_apple->x == snake_head->x && game_apple->y == snake_head->y) {
        points++;

        //spi_write16_blocking(SPI_PORT, &DAC_data_chomp, 1);
        if(points > high_score){
        high_score = points;
       }
        updateScoreDisplay(points);
        //speed up velocity by 5% every time we eat 5 apples
        if(points%5 == 0){
          cycles_per_frame -= cycles_per_frame/10;  
        }
        if(points%2==0 && points>0 && hard_mode==true){
              block_tail = addBlock(&block_tail, snake_head);
              drawBlock(block_tail);
            }
        generateNewApple(game_apple, snake_head);
        // extend snake
        snake_tail = addTail(&snake_tail);
        drawSnake(snake_tail, snake_tail, false);
      }
      
      //CHECK BLOCK DETECTION
      if(hard_mode==true){
        for(int i =0; i<40;i++){
          if((&block_objects[i])->x == snake_head->x && (&block_objects[i])->y == snake_head->y) {
          game_over=true;
          }
        }
      }
      // if(block_tail->x == snake_head->x && block_tail->y == snake_head->y) {
      //   game_over=true;}
      // check if head has collided with arena, if has, wallsAndEdges ends game
      wallsAndEdges(snake_head);
      if(snake_head->trailing != NULL) {
        if(!space_not_taken(snake_head->x, snake_head->y, snake_head->trailing)) {
          game_over = true;
        }
      }

      if(game_over){
        game_start = false;

        // SPI write (no spinlock b/c of SPI buffer)
        //spi_write16_blocking(SPI_PORT, &DAC_data_death, 1);

        //snake_head->trailing=NULL;
        //snake_tail->leading=NULL;
        //reinitializes the game with 3 length snake and no black blocks
        snake_tail = &snake_array[2];
        block_tail = block_tail_org;
        // (&block_objects[0])->x = -2;
        // (&block_objects[0])->y = -2;
        // (&block_objects[1])->x = -2;
        // (&block_objects[1])->y = -2;
        // (&block_objects[2])->x = -2;
        // (&block_objects[2])->y = -2;
        if(hard_mode==true){
          for(int i =0; i<40;i++){
          (&block_objects[i])->x = -2;
          (&block_objects[i])->y = -2;
          }
        }
        cycles_per_frame = 600;
        end_game();
        //gameStart();
      }
      
      
      spare_time = FRAME_RATE - (time_us_32() - begin_time) ;
      //Print on screen: # of boids, print FRAME_RATE (make sure its 30), and runtime
      printInfo(0,30.0,time_us_32());
      // yield for necessary amount of time
      // PT_YIELD_usec(spare_time) ;
  // NEVER exit while}
    } // END WHILE(1)
      
      
      //gameStart(snake_head,snake_tail,game_apple);
        //DO END GAME SEQUENCE OF THE SNAKE SHRINKING/GOING AWAY
        //maybe redraw the snake inside a while loop by decreasing the snake size every frame for 5 frames

         //DmaChnEnable(DMA_CHANNEL3); //Play the death audio when game ends

        //press any button to reset game

    }

  PT_END(pt);
} // animation thread


// This is the core 1 entry point. Essentially main() for core 1
void core1_entry()
{

    // create an alarm pool on core 1
    alarm_pool_t *core1pool;
    core1pool = alarm_pool_create(2, 16);

    // Create a repeating timer that calls repeating_timer_callback.
    struct repeating_timer timer_core_1;

    // Negative delay so means we will call repeating_timer_callback, and call it
    // again 25us (40kHz) later regardless of how long the callback took to execute
    alarm_pool_add_repeating_timer_us(core1pool, -25,
                                      repeating_timer_callback_core_1, NULL, &timer_core_1);

    // Add thread to core 1
    //pt_add_thread(protothread_core_1);      //SHOULD WE ADD OUR GAME PROTOTHREAD TO CORE1  ???
    

    // Start scheduler on core 1
    pt_schedule_start;
}

// ========================================
// === main
// ========================================
// USE ONLY C-sdk library
int main(){
  // initialize stio
  stdio_init_all() ;

  // initialize VGA
  initVGA() ;

  //Initialize buttons. get value w/ gpio_get(RIGHT)
  gpio_init(RIGHT);
  gpio_set_dir(RIGHT, GPIO_IN);
  gpio_pull_up(RIGHT);
  gpio_init(LEFT);
  gpio_set_dir(LEFT, GPIO_IN);
  gpio_pull_up(LEFT);
  gpio_init(UP);
  gpio_set_dir(UP, GPIO_IN);
  gpio_pull_up(UP);
  gpio_init(DOWN);
  gpio_set_dir(DOWN, GPIO_IN);
  gpio_pull_up(DOWN);

  // Initialize SPI channel (channel, baud rate set to 20MHz)
    spi_init(SPI_PORT, 20000000);
    // Format (channel, data bits per transfer, polarity, phase, order)
    spi_set_format(SPI_PORT, 16, 0, 0, 0);

// Populate the sine table and Hann window table

//FOR CHOMP SOUND:
    int iiDDS;
    for (iiDDS = 0; iiDDS < CHOMP_AUDIO_SIZE; iiDDS++)
    {
        Sinewave[iiDDS] = float2fix15(sin(6.283 * ((float)iiDDS) / (float)CHOMP_AUDIO_SIZE));
        window[iiDDS] = float2fix15(0.5 * (1.0 - cos(6.283 * ((float)iiDDS) / ((float)CHOMP_AUDIO_SIZE))));
    }


  //DINO GAME

    //CloseADC10(); //Close the ADC before configuring it

  // SetChanADC10(ADC_CH0_NEG_SAMPLEA_NVREF | ADC_CH0_POS_SAMPLEA_AN11);//Setup the ADC
  // OpenADC10(PARAM1, PARAM2, PARAM3, PARAM4, PARAM5); //Open the ADC using above parameters
    
  // EnableADC10(); //Enable the ADC
    
  // OpenTimer3(T3_ON | T3_SOURCE_INT | T3_PS_1_1, 5000); //Configure timer 3
  // SpiChnOpen(SPI_CHANNEL2, SPI_OPEN_ON | SPI_OPEN_MODE16 | SPI_OPEN_MSTEN | SPI_OPEN_CKE_REV | SPICON_FRMEN | SPICON_FRMPOL, SPI_CLK_DIV); //Configure SPI channel 2
  // PPSOutput(2, RPB5, SDO2); //Map SPI data out to RPB5
  // PPSOutput(4, RPA3, SS2); //Map SPI chip select to RPA3
    
  // DmaChnOpen(DMA_CHANNEL2, 3, DMA_OPEN_DEFAULT); //Open DMA channel 2
  // DmaChnSetTxfer(DMA_CHANNEL2, chompSound, (void*)&SPI2BUF, JUMP_AUDIO_SIZE, 2, 2); //Configure it to transmit jump audio
  // DmaChnSetEventControl(DMA_CHANNEL2, DMA_EV_START_IRQ(_TIMER_3_IRQ)); //Set DMA event control
    
  // DmaChnOpen(DMA_CHANNEL3, 0, DMA_OPEN_DEFAULT); //Open DMA channel 3
  // DmaChnSetTxfer(DMA_CHANNEL3, deathSound, (void*)&SPI2BUF, DEAD_AUDIO_SIZE, 2, 2); //Configure it to transmit dead audio
  // DmaChnSetEventControl(DMA_CHANNEL3, DMA_EV_START_IRQ(_TIMER_3_IRQ)); //Set DMA event control





    /////////////////////////////////////////////////////////////////////////////////
    // ============================== ADC DMA CONFIGURATION =========================
    /////////////////////////////////////////////////////////////////////////////////

//KEEP THIS
    // Channel configurations
    // dma_channel_config c2 = dma_channel_get_default_config(DMA_CHANNEL2CHOMP);
    // dma_channel_config c3 = dma_channel_get_default_config(DMA_CHANNEL3DEATH);

    // // ADC SAMPLE CHANNEL
    // // Reading from constant address, writing to incrementing byte addresses
    // channel_config_set_transfer_data_size(&c2, DMA_SIZE_8);
    // channel_config_set_read_increment(&c2, false);
    // channel_config_set_write_increment(&c2, true);
    // // Pace transfers based on availability of ADC samples
    // channel_config_set_dreq(&c2, DREQ_ADC);
    // // Configure the channel
    // dma_channel_configure(DMA_CHANNEL2CHOMP,
    //                       &c2,           // channel config
    //                       chompSound,  // dst
    //                       &adc_hw->fifo, // src
    //                       CHOMP_AUDIO_SIZE,   // transfer count
    //                       false          // don't start immediately
    // );

    // // Configure the channel
    // dma_channel_configure(DMA_CHANNEL3DEATH,
    //                       &c3,           // channel config
    //                       deathSound,  // dst
    //                       &adc_hw->fifo, // src
    //                       DEATH_AUDIO_SIZE,   // transfer count
    //                       false          // don't start immediately
    // );




    // // CONTROL CHANNEL
    // channel_config_set_transfer_data_size(&c3, DMA_SIZE_32); // 32-bit txfers
    // channel_config_set_read_increment(&c3, false);           // no read incrementing
    // channel_config_set_write_increment(&c3, false);          // no write incrementing
    // channel_config_set_chain_to(&c3, sample_chan);           // chain to sample chan

    // dma_channel_configure(
    //     DMA_CHANNEL3DEATH,                        // Channel to be configured
    //     &c3,                                 // The configuration we just created
    //     &dma_hw->ch[sample_chan].write_addr, // Write address (channel 0 read address)
    //     &sample_address_pointer,             // Read address (POINTER TO AN ADDRESS)
    //     1,                                   // Number of transfers, in this case each is 4 byte
    //     false                                // Don't start immediately.
    // );


    // Build the sine lookup table
    // scaled to produce values between 0 and 4096 (for 12-bit DAC)
    int ii;
    for (ii = 0; ii < sine_table_size; ii++)
    {
        sin_table[ii] = float2fix15(2047 * sin((float)ii * 6.283 / (float)sine_table_size));
    }

    // Initialize the intercore semaphores

    //IS THIS NECESSARY???
    PT_SEM_SAFE_INIT(&core_0_go, 1);
    PT_SEM_SAFE_INIT(&core_1_go, 0);

    // Launch core 1
    multicore_launch_core1(core1_entry);
  // add threads
  // pt_add_thread(protothread_serial);
  pt_add_thread(protothread_anim);

  // start scheduler
  pt_schedule_start ;
} 