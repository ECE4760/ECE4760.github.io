#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include <math.h>
#include "hardware/gpio.h"  
#include "pico/time.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "TFTMaster.h"
#include <string.h>
#include <pico/multicore.h>
#include "pt_cornell_rp2040_v1_3.h"
#include <hardware/gpio.h>
#include "hardware/spi.h"

// fruits
#define STATUS_PIXELS   20  // height of score bar
#define MAX_FRUITS     4

// Define GPIO pins for the direction buttons (adjust as needed for your wiring)
#define BUTTON_UP      6  //7  
#define BUTTON_LEFT    7  //8  
#define BUTTON_RIGHT   8  //6
#define BUTTON_DOWN    9  //9 
#define SPEAKER_PIN   18    

// FRAM (MB85RS64) SPI definitions
#define FRAM_SPI        spi0
#define FRAM_SCK_PIN    2
#define FRAM_MOSI_PIN   3
#define FRAM_MISO_PIN   4
#define FRAM_CS_PIN     5

// FRAM op-codes
#define FRAM_WREN       0x06
#define FRAM_WRITE      0x02
#define FRAM_READ       0x03




////////////////////////////////////////////////////// AI GENERATED FRAM INTERFACE LIBRARY BELOW!!!! //////////////////////////////////////////////////////
// Initialize SPI and CS pin
static void fram_init(void) {
    spi_init(FRAM_SPI, 2 * 1000 * 1000);              // 2 MHz
    gpio_set_function(FRAM_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(FRAM_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(FRAM_MISO_PIN, GPIO_FUNC_SPI);
    gpio_init(FRAM_CS_PIN);
    gpio_set_dir(FRAM_CS_PIN, GPIO_OUT);
    gpio_put(FRAM_CS_PIN, 1);
}

// Assert/deassert CS
static inline void fram_select(void)   { gpio_put(FRAM_CS_PIN, 0); }
static inline void fram_deselect(void) { gpio_put(FRAM_CS_PIN, 1); }

// Send WREN
static void fram_write_enable(void) {
    fram_select();
    uint8_t cmd = FRAM_WREN;
    spi_write_blocking(FRAM_SPI, &cmd, 1);
    fram_deselect();
}

// Write `len` bytes from `buf` into FRAM at 16-bit address `addr`
static void fram_write(uint16_t addr, const uint8_t *buf, size_t len) {
    fram_write_enable();
    uint8_t hdr[3] = {
        FRAM_WRITE,
        (uint8_t)(addr >> 8),
        (uint8_t)(addr & 0xFF)
    };
    fram_select();
    spi_write_blocking(FRAM_SPI, hdr, 3);
    spi_write_blocking(FRAM_SPI, buf, len);
    fram_deselect();
}

// Read `len` bytes into `buf` from FRAM at 16-bit address `addr`
static void fram_read(uint16_t addr, uint8_t *buf, size_t len) {
    uint8_t hdr[3] = {
        FRAM_READ,
        (uint8_t)(addr >> 8),
        (uint8_t)(addr & 0xFF)
    };
    fram_select();
    spi_write_blocking(FRAM_SPI, hdr, 3);
    spi_read_blocking(FRAM_SPI, 0x00, buf, len);
    fram_deselect();
}
//////////////////////////////////////////////////////// AI GENERATED  CODE ABOVE!!!! //////////////////////////////////////////////////////

////////////////////////////////////////////////////// AI GENERATED SOUNDS BELOW!!!! //////////////////////////////////////////////////////
/////////////////////////MUSIC SECTION//////////////////////////////
typedef struct {
    uint32_t freq;      // in Hz; use 0 for a rest
    uint32_t dur_ms;    // duration in milliseconds
} note_t;


static const note_t background_tune[] = {
    {392, 120},  // G4
    {311, 120},  // Eb4
    {349, 120},  // F4
    {311, 120},  // Eb4
    {392, 120},  // G4
    {415, 120},  // Ab4
    {466, 120},  // Bb4
    {523, 240},  // C5
    {  0,  60},  // rest
    {523, 120},  // C5
    {466, 120},  // Bb4
    {349, 120},  // F4
    {311, 120},  // Eb4
    {  0, 120},  // rest
};

static const note_t menu_tune[] = {
    {523, 300},  // C5
    {659, 300},  // E5
    {784, 300},  // G5
    {523, 300},  // C5 (octave loop)
    {880, 300},  // A5
    {784, 300},  // G5
    {659, 300},  // E5
    {523, 600},  // C5 hold
    {0,   200},  // rest before looping
};

extern const int     background_tune_len;
static int bg_note_index = 0;
static const int menu_tune_len = sizeof(menu_tune)/sizeof(menu_tune[0]);
// timers and indices
static repeating_timer_t menu_timer, bg_timer;
static int               menu_idx = -1, bg_idx = -1;
static bool              menu_playing = false, bg_playing = false;

///////////////////////////////////////////////////////////////////////////////////////////////////
//Speaker
///////////////////////////////////////////////////////////////////////////////////////////////////

void play_tone(uint pin, uint32_t freq, uint32_t duration_ms) {
    // 1) route the pin to PWM hardware
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_clkdiv(slice, 125.0f);
    uint32_t wrap = 1000000 / freq - 1;
    pwm_set_wrap(slice, wrap);
    pwm_set_chan_level(slice, pwm_gpio_to_channel(pin), wrap/2);
    pwm_set_enabled(slice, true);
    sleep_ms(duration_ms);
    pwm_set_enabled(slice, false);
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_put(pin, 0);
}
// configure PWM for a given freq, but don’t sleep
void tone_start(uint pin, uint32_t freq) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_clkdiv(slice, 125.0f);  
    uint32_t wrap = 1000000/freq - 1;
    pwm_set_wrap(slice, wrap);
    pwm_set_chan_level(slice, pwm_gpio_to_channel(pin), wrap/2);
    pwm_set_enabled(slice, true);
}
// immediately silence the pin
void tone_stop(uint pin) {
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_enabled(slice, false);
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_put(pin, 0);
}


bool bg_music_step(struct repeating_timer *t) {
    // stop whatever’s playing right now
    tone_stop(SPEAKER_PIN);

    // advance (and wrap) the tune index
    bg_note_index = (bg_note_index + 1) % 
        (sizeof(background_tune)/sizeof(note_t));

    // start the next note
    tone_start(SPEAKER_PIN,
               background_tune[bg_note_index].freq);

    // and re‑schedule ourselves for that note’s duration
    cancel_repeating_timer(&bg_timer);
    add_repeating_timer_ms(
        background_tune[bg_note_index].dur_ms,
        bg_music_step, NULL, &bg_timer
    );
    return false;  // we manually re‑scheduled
}
void start_background_music() {
    if (bg_playing) return;
    bg_playing     = true;
    bg_note_index  = -1;            // so first call jumps to index 0
    bg_music_step(NULL);            // kick‑off immediately
}

// menu jingle step
bool menu_step(struct repeating_timer *t) {
    tone_stop(SPEAKER_PIN);
    menu_idx = (menu_idx + 1) % menu_tune_len;
    uint32_t f = menu_tune[menu_idx].freq;
    if (f) tone_start(SPEAKER_PIN, f);
    cancel_repeating_timer(&menu_timer);
    add_repeating_timer_ms(menu_tune[menu_idx].dur_ms,
                           menu_step, NULL, &menu_timer);
    return false;
  }
  
  // background jingle step
  bool bg_step(struct repeating_timer *t) {
    tone_stop(SPEAKER_PIN);
    bg_idx = (bg_idx + 1) % background_tune_len;
    uint32_t f = background_tune[bg_idx].freq;
    if (f) tone_start(SPEAKER_PIN, f);
    cancel_repeating_timer(&bg_timer);
    add_repeating_timer_ms(background_tune[bg_idx].dur_ms,
                           bg_step, NULL, &bg_timer);
    return false;
  }
  

void start_menu_music() {
    if (menu_playing) return;
    menu_playing = true;
    menu_idx     = -1;
    menu_step(NULL);    // see next section
  }
  
  void stop_menu_music() {
    if (!menu_playing) return;
    menu_playing = false;
    cancel_repeating_timer(&menu_timer);
    tone_stop(SPEAKER_PIN);
  }
    


void stop_background_music() {
    if (!bg_playing) return;
    bg_playing = false;
    cancel_repeating_timer(&bg_timer);
    tone_stop(SPEAKER_PIN);
}

void resume_background_music() {
    if (bg_playing) return;
    bg_playing = true;
    // schedule the very next note immediately
    add_repeating_timer_ms(0, bg_music_step, NULL, &bg_timer);
}


// Call this when the snake eats food for a quick descending jingle
void play_eat_sound() {
    // Eb6 → D6 → C6 → G#5, with tight decay
    play_tone(SPEAKER_PIN, 1245, 50);  // Eb6
    sleep_ms(25);
    play_tone(SPEAKER_PIN, 1175, 50);  // D6
    sleep_ms(25);
    play_tone(SPEAKER_PIN, 1047, 60);  // C6
    sleep_ms(30);
    play_tone(SPEAKER_PIN, 830,  80);  // G#5
    sleep_ms(40);

    resume_background_music();
}

void play_death_sound() {
    // C5 → B4 → Bb4 → A4 → E4 → A2, with low rumble and final silence
    play_tone(SPEAKER_PIN, 523,  80);  // C5 – sharp start
    sleep_ms(40);
    play_tone(SPEAKER_PIN, 493,  80);  // B4 – minor step down
    sleep_ms(40);
    play_tone(SPEAKER_PIN, 466,  80);  // Bb4 – tritone tension
    sleep_ms(40);
    play_tone(SPEAKER_PIN, 440, 120);  // A4 – settling
    sleep_ms(60);
    play_tone(SPEAKER_PIN, 330, 200);  // E4 – deeper drop
    sleep_ms(100);
    play_tone(SPEAKER_PIN, 110, 300);  // A2 – low rumble
    sleep_ms(150);
}

////////////////////////////////////////////////////// AI GENERATED SOUNDS ABOVE!!!! //////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////////////////////
//GAME BOARD
//////////////////////////////////////////////////////////////////////////////////////////////////////////
// Point structure to represent positions on the grid
typedef struct {
    int x;
    int y;
} Point;

static int screen_w, screen_h;
static int cell_size;
static int playable_h, grid_w, grid_h;
// Enum for the overall game states.
typedef enum {
    STATE_START,   // Welcome screen
    STATE_PLAYING, // Game in progress
    STATE_END      // Game over screen
} GameState;

static GameState game_state;    // Game state variable
static GameState prev_state;
static int score = 0;           // Private variable to track the score
static int high_score;
// Global game variables
Point *snake;   // Array holding positions of the snake segments
int snake_length;               // Current length of the snake
int MAX_SNAKE_LENGTH;



// dynamically calculattes the grid based on cell_size 
void calculate_grid() {
    screen_w    = tft_width();
    screen_h    = tft_height();
    playable_h  = screen_h - STATUS_PIXELS;
    grid_w      = screen_w  / cell_size;
    grid_h      = playable_h / cell_size;
    MAX_SNAKE_LENGTH = grid_h * grid_h;
    snake = malloc(MAX_SNAKE_LENGTH * sizeof *snake);
  }

// Display the start screen.
void start_screen() {
    tft_fillScreen(ILI9340_BLACK);
    tft_setTextColor(ILI9340_WHITE);
    tft_setTextSize(4);
    // Centered title. Adjust cursor positions as needed.
    tft_setCursor(ILI9340_TFTWIDTH/ 2 - 75, ILI9340_TFTHEIGHT / 2 - 80);
    tft_writeString("Snake Game");
}

// Display the game over screen along with the final score.
void end_screen() {
    char score_text[32];
    tft_fillScreen(ILI9340_BLACK);

    if( score == MAX_SNAKE_LENGTH - 1){
        tft_setTextColor(ILI9340_YELLOW);
        tft_setTextSize(5);
        tft_setCursor(ILI9340_TFTWIDTH/ 2 - 65, ILI9340_TFTHEIGHT / 2 - 140);
        tft_writeString("VICTORY");
        tft_setTextColor(ILI9340_WHITE);
    }
    else{
        tft_setTextColor(ILI9340_WHITE);
        tft_setTextSize(4);
        tft_setCursor(ILI9340_TFTWIDTH/ 2 - 65, ILI9340_TFTHEIGHT / 2 - 140);
        tft_writeString("Game Over");
    }
    

    tft_setTextSize(2.5);
    
    if(score > high_score){
        high_score = score;

        // persist to FRAM
        uint8_t out[2] = {
            (uint8_t)(high_score >> 8),
            (uint8_t)(high_score & 0xFF)
        };
        fram_write(0x0000, out, 2);

        tft_setTextColor(ILI9340_RED);
        sprintf(score_text, "New High Score!");
        tft_setCursor(ILI9340_TFTWIDTH/ 2 - 50, ILI9340_TFTHEIGHT / 2 - 80);
        tft_writeString(score_text);
        sprintf(score_text, "Score: %d", score);
        tft_setCursor(ILI9340_TFTWIDTH/ 2 - 20, ILI9340_TFTHEIGHT / 2 - 40);
        tft_writeString(score_text);
        tft_setTextColor(ILI9340_WHITE);
    }// Format the final score as a string.
    else{
        sprintf(score_text, "High Score: %d", high_score);
        tft_setCursor(ILI9340_TFTWIDTH/ 2 - 40, ILI9340_TFTHEIGHT / 2 - 80);
        tft_writeString(score_text);
        tft_setTextColor(ILI9340_WHITE);
        sprintf(score_text, "Score: %d", score);
        tft_setCursor(ILI9340_TFTWIDTH/ 2 - 10, ILI9340_TFTHEIGHT / 2 - 40);
        tft_writeString(score_text);
    }
    
}
// draws a green/lightgreen checkerboard pattern playing grid
void draw_background() {
    for (int y = 0; y < grid_h; y++) {
        for (int x = 0; x < grid_w; x++) {
            uint16_t color = ((x + y) % 2 == 0) ? ILI9340_GREEN : ILI9340_LIGHTGREEN;
            tft_fillRect(x * cell_size, y * cell_size, cell_size, cell_size, color);
        }
    }
    tft_fillRect(0, playable_h, screen_w, STATUS_PIXELS,ILI9340_BLACK);
}
// generates a status bar that displays the score.
void draw_score() {
    char buf[16];
    sprintf(buf, "Score: %d", score);
    tft_setTextColor(ILI9340_WHITE);
    tft_setTextSize(2);
    // position text inside that bottom 20 px
    tft_setCursor(5, playable_h + 4);
    tft_writeString(buf);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//FRUITS
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static Point fruits[MAX_FRUITS];
static int   current_fruits;


static void spawn_one_fruit(Point *f) {
    bool valid=false;
    while(!valid){
        f->x = rand()%grid_w;
        f->y = rand()%grid_h;
        valid=true;
        // check snake
        for(int i=0;i<snake_length;i++) 
            if(snake[i].x == f->x && snake[i].y == f->y) {
                valid = false;
                break;
        }
        // check other fruits
        for(int j=0;j<MAX_FRUITS && valid;j++){
            if(&fruits[j] != f && fruits[j].x == f->x && fruits[j].y == f->y){
                valid = false;
            }
        }
    }
}

// Spawn food at a random grid location not occupied by the snake.
static void spawn_all_fruits() {
    for (int i = 0; i < MAX_FRUITS; i++) {
        spawn_one_fruit(&fruits[i]);
    }
}

// draws all fruits onto the map
static void draw_all_fruits() {
    for (int i = 0; i < current_fruits; i++) {
        tft_fillRect(fruits[i].x * cell_size, fruits[i].y * cell_size, cell_size, cell_size, ILI9340_RED);
    }
}

// recompute how many fruits to show (1..MAX_FRUITS)
static void update_fruit_count() {
    current_fruits = 1 + ((MAX_FRUITS - 1) * (MAX_SNAKE_LENGTH - snake_length)) / MAX_SNAKE_LENGTH;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Buttons
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enum for the four movement directions.
typedef enum {UP, DOWN, LEFT, RIGHT} Direction;
Direction current_direction;    

// Flags for ISR
volatile bool up_pressed = false;
volatile bool left_pressed = false;
volatile bool right_pressed = false;
volatile bool down_pressed = false;

//checks for which button press
void gpio_callback(uint gpio, uint32_t events) {
    if (gpio == BUTTON_UP) {
        up_pressed = true;
    } else if (gpio == BUTTON_DOWN) {
        down_pressed = true;
    } else if (gpio == BUTTON_LEFT) {
        left_pressed = true;
    } else if (gpio == BUTTON_RIGHT) {
        right_pressed = true;
    }
}

bool any_button_pressed() {
    return up_pressed || down_pressed || left_pressed || right_pressed;
}

// Process button inputs to update the snake's movement direction.
void process_input() {
    if (up_pressed && current_direction != DOWN) {
        current_direction = UP;
    } else if (down_pressed && current_direction != UP) {
        current_direction = DOWN;
    } else if (left_pressed && current_direction != RIGHT) {
        current_direction = LEFT;
    } else if (right_pressed && current_direction != LEFT) {
        current_direction = RIGHT;
    }
    up_pressed = down_pressed = left_pressed = right_pressed = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Gameplay
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Initialize or restart the game.
void init_game() {
    // Reset the score.
    score = 0;
    
    draw_background();
    draw_score();
    // Set initial snake length and starting position (center of the grid).
    snake_length = 3;
    int start_x = grid_w / 2;
    int start_y = grid_h / 2;
    for (int i = 0; i < snake_length; i++) {
        snake[i].x = start_x - i;  // Snake initially oriented horizontally.
        snake[i].y = start_y;
        tft_fillRect(snake[i].x * cell_size, snake[i].y * cell_size, cell_size, cell_size, ILI9340_BLUE);
    }
    
    // Start moving to the right.
    current_direction = RIGHT;
    
    // Generate and draw the first food item.
    update_fruit_count();
    spawn_all_fruits();
    draw_all_fruits();
}

// The screen‐manager thread on core 1
static PT_THREAD (screen_pt(struct pt *pt)){
    PT_BEGIN(pt);
    
    while (1) {
        if (game_state == STATE_START){
            tft_fillScreen(ILI9340_BLACK);
            tft_setTextColor(ILI9340_WHITE);
            start_menu_music();
            start_screen();
            tft_setTextSize(1);

            while(game_state == STATE_START){
                tft_fillRect(ILI9340_TFTWIDTH / 2 -  60, ILI9340_TFTHEIGHT / 2-30, 300, 50, ILI9340_BLACK);
                // difficulty selection
                if (any_button_pressed()) {
                    if (left_pressed){
                        cell_size = 20;
                    }
                    else if (up_pressed){
                        cell_size = 16;
                    }
                    else if (right_pressed){
                        cell_size = 12;
                    }
                    else{
                        cell_size = 10;
                    }

                    //Reset the flags
                    up_pressed    = false;
                    down_pressed  = false;
                    left_pressed  = false;
                    right_pressed = false;

                    //Adjust grid based on difficulty
                    calculate_grid();
                    game_state = STATE_PLAYING;
                    stop_menu_music();
                    play_tone(SPEAKER_PIN, 523, 100);
                    break;
                    
                }

                sleep_ms(500);
                tft_setCursor(ILI9340_TFTWIDTH / 2 -  32, ILI9340_TFTHEIGHT / 2-20);
                tft_writeString("Left = Easy, Up = Normal");
                tft_setCursor(ILI9340_TFTWIDTH / 2 - 40, ILI9340_TFTHEIGHT / 2);
                tft_writeString("Right = Hard, Down = Insane");
                sleep_ms(500);

            }
        }
        if (game_state == STATE_END){
            stop_background_music();
            stop_menu_music();
            tft_fillScreen(ILI9340_BLACK);
            tft_setTextColor(ILI9340_WHITE);
            end_screen();
            tft_setTextSize(2);

            while(game_state == STATE_END){
                tft_fillRect(ILI9340_TFTWIDTH / 2 -  60, ILI9340_TFTHEIGHT / 2, 300, 50, ILI9340_BLACK);
                if (any_button_pressed()) {
                    // clear flags
                    up_pressed    = false;
                    down_pressed  = false;
                    left_pressed  = false;
                    right_pressed = false;
                    
                    //bring us back to start menu so we can select new difficulty
                    game_state = STATE_START;
                    stop_menu_music();
                    play_tone(SPEAKER_PIN, 523, 100);
                    break;
                }
                
                sleep_ms(500);
                tft_setCursor(ILI9340_TFTWIDTH/ 2 - 55, ILI9340_TFTHEIGHT / 2 + 5);
                tft_writeString("Press any button");
                tft_setCursor(ILI9340_TFTWIDTH/ 2 - 15, ILI9340_TFTHEIGHT / 2 + 30);
                tft_writeString("to restart");
                sleep_ms(500);
            }
        }
        PT_YIELD_usec(100000);
            
    }
        
    PT_END(pt);
}

// --------------------------------------------------------------------
// Protothread: game logic (input, snake movement, collision, drawing)
// --------------------------------------------------------------------

static PT_THREAD(game_thread(struct pt *pt)) {
    PT_BEGIN(pt);
    while (1) {
        // Game loop
        while (game_state == STATE_PLAYING) {
            if (prev_state != game_state){
                init_game(); 
                start_background_music();
                
            }
            process_input();
            // compute new head
            Point nh = snake[0];
            switch (current_direction) {
                case UP:    nh.y--; break;
                case DOWN:  nh.y++; break;
                case LEFT:  nh.x--; break;
                case RIGHT: nh.x++; break;
            }
            // boundary collision
            if (nh.x < 0 || nh.x >= grid_w || nh.y < 0 || nh.y >= grid_h) {
                game_state = STATE_END;
                prev_state = STATE_END;
                stop_background_music();
                play_death_sound();
                break;
            }
            // self-collision
            for (int i = 0; i < snake_length; i++) {
                if (snake[i].x == nh.x && snake[i].y == nh.y) {
                    game_state = STATE_END;
                    prev_state = STATE_END;
                    stop_background_music();
                    play_death_sound();
                    break;
                }
            }
            if (game_state != STATE_PLAYING) break;

            // check if snake ate a fruit
            bool ate_any=false;
            for(int i=0;i<current_fruits;i++){
                if(nh.x==fruits[i].x && nh.y==fruits[i].y){
                    ate_any=true; 
                    score++; 
                    if(snake_length<MAX_SNAKE_LENGTH) snake_length++;
                    update_fruit_count(); 
                    spawn_one_fruit(&fruits[i]);
                    tft_fillRect(0,playable_h,screen_w,STATUS_PIXELS,ILI9340_BLACK);
                    draw_score(); 
                    draw_all_fruits(); 
                    play_eat_sound();
                    break;
                }
            }

            if(!ate_any) {
                // erase tail
                Point tail=snake[snake_length-1];
                uint16_t bg=((tail.x+tail.y)%2==0)?ILI9340_GREEN:ILI9340_LIGHTGREEN;
                tft_fillRect(tail.x*cell_size,tail.y*cell_size,cell_size,cell_size,bg);
            }
            // shift & draw
            for(int i=snake_length-1; i>0 ;i--) snake[i] = snake[i-1];
            snake[0] = nh;
            tft_fillRect(nh.x*cell_size,nh.y*cell_size,cell_size,cell_size,ILI9340_BLUE);
            prev_state=game_state;
            sleep_ms(200);
        }
        PT_YIELD_usec(100000);
    }
    PT_END(pt);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




// Core 1 entry
void core1_entry() {
    pt_add_thread(screen_pt) ;
    pt_schedule_start ;
}

int main() {
    // Initialize standard I/O and the TFT display.
    stdio_init_all();
    tft_init_hw();
    tft_begin();
    tft_setRotation(3);
    fram_init();

    // read 2-byte high score from FRAM addr 0x0000
    uint8_t hs_buf[2];
    fram_read(0x0000, hs_buf, 2);
    high_score = ((uint16_t)hs_buf[0] << 8) | hs_buf[1];

    tft_fillScreen(ILI9340_BLACK);
    
    // Initialize button pins as inputs with internal pull-ups.
    gpio_init(BUTTON_UP);
    gpio_set_dir(BUTTON_UP, GPIO_IN);
    gpio_pull_up(BUTTON_UP);

    gpio_init(BUTTON_DOWN);
    gpio_set_dir(BUTTON_DOWN, GPIO_IN);
    gpio_pull_up(BUTTON_DOWN);
    
    gpio_init(BUTTON_LEFT);
    gpio_set_dir(BUTTON_LEFT, GPIO_IN);
    gpio_pull_up(BUTTON_LEFT);
    
    gpio_init(BUTTON_RIGHT);
    gpio_set_dir(BUTTON_RIGHT, GPIO_IN);
    gpio_pull_up(BUTTON_RIGHT);
    
    // register the SINGLE callback for all four pins
    gpio_set_irq_enabled_with_callback(BUTTON_UP,    GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled_with_callback(BUTTON_DOWN,  GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled_with_callback(BUTTON_LEFT,  GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled_with_callback(BUTTON_RIGHT, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    // Seed the random number generator.
    srand(to_ms_since_boot(get_absolute_time()));
    // Start with the start screen.
    game_state = STATE_START;
    prev_state = STATE_START;
    multicore_reset_core1();
    multicore_launch_core1(core1_entry);
    pt_add_thread(game_thread);
    pt_schedule_start;
}
