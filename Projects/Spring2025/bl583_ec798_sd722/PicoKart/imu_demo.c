/**
 * Controls a 1-DOF helicopter arm using MPU6050 IMU data and PID target_ang.
 * System features:
 * - Reads accelerometer/raw_gyro data from MPU6050
 * - Estimates arm angle using complementary filter
 * - PID controller maintains desired arm angle
 * - Real-time VGA display (top: PID output, bottom: angle)
 * - Serial interface for angle/PID tuning
 *
 * pedals: brown: gnd, purple: power, red: gpio
 *
 *
 * HARDWARE CONNECTIONS
 * Motor Control:
 *  - GPIO 4  ---> PWM output
 *
 *  VGA Display:
 *  - GPIO 16 ---> VGA Hsync
 *  - GPIO 17 ---> VGA Vsync
 *  - GPIO 18 ---> 470 ohm resistor ---> VGA Green
 *  - GPIO 19 ---> 330 ohm resistor ---> VGA Green
 *  - GPIO 20 ---> 330 ohm resistor ---> VGA Blue
 *  - GPIO 21 ---> 330 ohm resistor ---> VGA Red
 *  - RP2040 GND ---> VGA GND
 */

// Include standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
// Include PICO libraries
#include "pico/stdlib.h"
#include "pico/multicore.h"
// Include hardware libraries
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/adc.h"
#include "hardware/pio.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"
#include "hardware/spi.h"
// Include custom libraries
#include "vga16_graphics.h"
#include "mpu6050.h"
#include "pt_cornell_rp2040_v1_3.h"
#include "character.h"
#include "tree.h"
#include "clock.h"
#include "intro.h"
#include "pico/rand.h"
// sound
// #include "sounds/moo.h"
#include "sounds/sfx.h"
#include "youwins.h"

// DAC configuration for Channel A and Channel B
#define DAC_CONFIG_CHAN_A 0b0011000000000000
#define DAC_CONFIG_CHAN_B 0b1011000000000000

#define YOUWIN_WIDTH 320
#define YOUWIN_HEIGHT 240
extern const unsigned short vga_image_win[YOUWIN_WIDTH * YOUWIN_HEIGHT];

#define PIN_MISO 4
#define PIN_CS 5
#define PIN_SCK 6
#define PIN_MOSI 7
#define LDAC 8

// DMA channels
int data_channel, control_channel;

// Sine table and DAC data for audio output
#define SINE_TABLE_SIZE 256
int raw_sin[SINE_TABLE_SIZE];
unsigned short DAC_data[SINE_TABLE_SIZE];
unsigned short *dac_data_pointer = &DAC_data[0];

// SPI configurations (note these represent GPIO number, NOT pin number)
#define PIN_MISO 4
#define PIN_CS 5
#define PIN_SCK 6
#define PIN_MOSI 7
#define LDAC 8
#define NORMAL_VELOCITY 5

// SPI Configuration settings for RP2040
#define SPI_PORT spi0
#define SPI_BAUDRATE 500000 // Set baudrate for SPI communication

#define TREE_WIDTH 100
#define TREE_HEIGHT 100

#define LEFT_TREE_X 30
#define RIGHT_TREE_X (SCREEN_WIDTH - 30 - TREE_WIDTH)
#define TREE_Y SKY_HEIGHT + 50

#define FLOWER_SIZE 10 // try 4×4 pixels
#define MAX_VELOCITY 7
#define MIN_VELOCITY 0
#define GRASS_COLOR GREEN

#define CLOCKTOWER_WIDTH 133
#define CLOCKTOWER_HEIGHT 200
#define CLOCKTOWER_X (SCREEN_WIDTH / 2 - CLOCKTOWER_WIDTH / 2)
#define CLOCKTOWER_Y 60

// Add this with other global variables
volatile float velocity_accumulator = 0.0f;
#define NORMAL_VELOCITY 5
#define ACCUMULATION_RATE 0.1f
#define ACCUMULATOR_DECAY 0.1f
#define MAX_ACCUMULATOR 5.0f

#define MAX_SCANLINE 480

#define BUTTON_PIN 15

#define SPEED_BOOST 8
volatile uint16_t background_stats = LIGHT_BLUE;

static int prev_left_edges[MAX_SCANLINE];
static int prev_right_edges[MAX_SCANLINE];
static int prev_center_x[MAX_SCANLINE];
static int prev_dash_width[MAX_SCANLINE];
static bool prev_had_dash[MAX_SCANLINE];

static const float CURVE_PERIOD = 240.0f;
// SUPER curvy is 50
static float amplitude = 0.0f;
static float curve_phase = 0.0f;
static const float curve_speed = 0.003f;

#define GRASS_COLOR GREEN
static const uint16_t FLOWER_COLORS[] = {LIGHT_PINK, YELLOW, WHITE, MAGENTA, ORANGE, LIGHT_BLUE, PINK, RED, DARK_ORANGE};
#define NUM_FLOWERS 150

#define MAX_SCANLINE 480

static int prev_left_edges[MAX_SCANLINE];
static int prev_right_edges[MAX_SCANLINE];
static bool road_first_frame = true;

typedef struct
{
    int x, y;
    uint16_t color;
} Flower;
static Flower flowers[NUM_FLOWERS];

enum PowerUps
{
    Non,

    Martha,    // pumpkin
    Speed,     // bell
    Invincible // dragon
};

enum Obstacles
{
    None,
    Cow,    // cow
    Bear,   // bear
    Banana, // banana
};

enum Weather
{
    Morning,
    Sunset,
    Night
};

typedef struct
{
    int x;
    int y;
    int width;
    int height;
    int prev_x;
    int prev_y;
    float prev_scale;
    enum PowerUps p;
    enum Obstacles o;
    bool active;
    bool needs_erase;
} Objects;

enum Weather w = Morning;
bool update_background = false;
volatile int weather_duration = -1;

fix15 boost = 1.5;
bool invincible = false;
bool speed_up = false;
volatile int speed_duration = -1;
volatile int invincible_duration = -1;
;

// Road variable
volatile float road_scroll = 0.0f;

// for hill: 150, 150.0f, 550.0f
#define SKY_HEIGHT 160

#define POT0_ADC_CH 0 // GPIO27
#define POT1_ADC_CH 1 // GPIO27
#define POT2_ADC_CH 2 // GPIO28
#define POT0_MAX_MM 50.0f
#define POT1_MAX_MM 50.0f
#define POT2_MAX_MM 50.0f

// FOR NOW we define the track as following, a lap is 80k:
#define LAP_LENGTH 80000

// From 10k - 20k we mildy increase the road to a cap
#define DIST_MILD_START 10000
#define DIST_MILD_END 20000
#define AMP_MILD_CAP 20.0f
// We increase the amplitude every MILD_CHANGE units
#define MILD_CHANGE 400

// From 20 - 30k we go from the mild cap to the max cap
#define DIST_MAX_START 20000
#define DIST_MAX_END 30000
#define AMP_MAX_CAP 80.0f
// We increase the amplitude every MAX_CHANGE units
#define MAX_CHANGE 150

// From 50k and onward we go back to an amplitude of 0
#define DIST_STRAIGHT_START 50000
// Decrease every STRAIGHT_CHANGE units
#define STRAIGHT_CHANGE 250

char display_text[40]; // Display text buffer

#define MAX_OBSTACLES 3

Objects obstacles[MAX_OBSTACLES];

static struct pt_sem vga_sem;
#define min(a, b) ((a < b) ? a : b)
#define max(a, b) ((a < b) ? b : a)
#define abs(a) ((a > 0) ? a : -a)

#define WRAPVAL 1000
#define CLKDIV 25.0
uint pwm_slice;

#define KART_WIDTH 20
#define SCREEN_WIDTH 640
#define RECT_WIDTH 20
#define ADC_MAX 4095
#define UPDATE_FACTOR 20

#define VGA_HEIGHT 480

volatile int distance = 0;
volatile int time = 0;
volatile int laps = 0;

typedef struct
{
    float x;
    int y;
    int height;
    int width;
    int velocity;
    bool invincible;
} Racer;

Racer racer = {320, 400, 42, 42, 5, false};

static inline float getRoadCurve(int y)
{
    return amplitude * sinf(curve_phase + 2.0f * 3.14159f * ((float)y + road_scroll) / CURVE_PERIOD);
}

bool collision(float obj_x, int obj_width, float obj_y, int obj_height)
{

    // Get the boundaries of the kart
    float kart_left = racer.x;
    float kart_right = racer.x + racer.width;
    float kart_top = racer.y;
    float kart_bottom = racer.y + racer.height;

    // Get the boundaries of the obstacle
    float obj_left = obj_x;
    float obj_right = obj_x + obj_width;
    float obj_top = obj_y;
    float obj_bottom = obj_y + obj_height;

    // drawRect(obj_left, obj_top, obj_width, obj_height, RED);

    // Check for overlap in both x and y directions
    bool x_overlap = !(kart_right < obj_left || kart_left > obj_right);
    bool y_overlap = !(kart_bottom < obj_top || kart_top > obj_bottom);

    // Return true if there is overlap and the kart is not invincible
    return x_overlap && y_overlap && !racer.invincible;
}

static void show_win_screen(void)
{
    // 1) clear to black (or whatever background)
    fillRect(0, 0, SCREEN_WIDTH, VGA_HEIGHT, RED);

    // 2) center the image (optional — if you want full-screen, set x0=y0=0)
    const int w = YOUWIN_WIDTH;
    const int h = YOUWIN_HEIGHT;
    const int x0 = (SCREEN_WIDTH - w) / 2;
    const int y0 = (VGA_HEIGHT - h) / 2;

    // 3) blit it pixel-by-pixel
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            uint8_t color = vga_image_win[y * w + x] & 0x0F;
            drawPixel(x0 + x, y0 + y, color);
        }
    }

    // 4) optionally wait for the user to hit the button before continuing
    while (gpio_get(BUTTON_PIN))
    {
        tight_loop_contents();
    }
}

static inline void drawObject(int x, int y, bool obstacle, int type, float ratio, bool erase)
{
    // Draw the object at the specified position with the given color and scale
    if (obstacle)
    {
        if (type == Cow)
        {
            if (erase)
            {
                draw_cow_bitmask_scaled(x, y, BLACK, ratio);
            }
            else
            {
                draw_cow_bitmask_scaled(x, y, MED_GREEN, ratio);
            }
        }
        else if (type == Bear)
        {
            if (erase)
            {
                draw_bear_bitmask_scaled(x, y, BLACK, ratio);
            }
            else
            {
                draw_bear_bitmask_scaled(x, y, DARK_ORANGE, ratio);
            }
        }
        else if (type == Banana)
        {
            if (erase)
            {
                draw_banana_bitmask_scaled(x, y, BLACK, ratio);
            }
            else
            {
                draw_banana_bitmask_scaled(x, y, YELLOW, ratio);
            }
        }
    }
    else
    {
        if (type == Speed)
        {
            if (erase)
            {
                draw_bell_bitmask_scaled(x, y, BLACK, ratio);
            }
            else
            {
                draw_bell_bitmask_scaled(x, y, YELLOW, ratio);
            }
        }
        else if (type == Invincible)
        {
            if (erase)
            {
                draw_dragon_bitmask_scaled(x, y, BLACK, ratio);
            }
            else
            {
                draw_dragon_bitmask_scaled(x, y, MAGENTA, ratio);
            }
        }
        else if (type == Martha)
        {
            if (erase)
            {
                draw_pumpkin_bitmask_scaled(x, y, BLACK, ratio);
            }
            else
            {
                draw_pumpkin_bitmask_scaled(x, y, ORANGE, ratio);
            }
        }
    }
}

typedef struct
{
    int x, y, speed;
    float scale;
} Cloud;

static Cloud clouds[CLOUD_COUNT];

static int prev_kart_x = 320;

// scatter clouds randomly across the sky height
void init_clouds()
{
    for (int i = 0; i < CLOUD_COUNT; i++)
    {
        clouds[i].x = get_rand_32() % SCREEN_WIDTH;
        clouds[i].y = 70 + (get_rand_32() % (SKY_HEIGHT - 95));
        clouds[i].speed = 1 + (get_rand_32() % 3);                   // speed = 1 or 2 px/frame
        clouds[i].scale = 1.25f + ((float)get_rand_32() / RAND_MAX); // scale = 1.0 to <3.0
    }
}

void draw_cloud(const Cloud *c, uint16_t color)
{
    draw_cloud_bitmask_scaled(c->x, c->y, color, c->scale);
}

void draw_dynamic_road()
{
    const int road_center = 320;
    const int base_road_width = 600;
    const uint16_t ROAD_COLOR = BLACK;
    const uint16_t ROAD_BACKGROUND = GRASS_COLOR;
    const uint16_t CENTER_LINE_COLOR = WHITE;

    // Define dash parameters
    const int dash_length_base = 20;       // Base length of each dash at bottom of screen
    const int dash_gap_base = 20;          // Base length of gap between dashes
    const float dash_width_factor = 0.04f; // Width of dash as fraction of road width

    static bool first_frame = true;
    static float dash_offset = 0.0f; // Offset for dash animation

    // Define car area to avoid drawing over it
    const int car_left = racer.x;
    const int car_right = racer.x + 40;
    const int car_top = 400;
    const int car_bottom = 440;

    // Initialize arrays on first run
    if (first_frame)
    {
        for (int y = 0; y < 480; y++)
        {
            prev_left_edges[y] = road_center - base_road_width / 2;
            prev_right_edges[y] = road_center + base_road_width / 2;
            prev_center_x[y] = road_center;
            prev_dash_width[y] = 0;
            prev_had_dash[y] = false;
        }
        // Clear a wider road area
        fillRect(100, SKY_HEIGHT, 440, 480 - SKY_HEIGHT, ROAD_BACKGROUND);

        // for (int i = 0; i < NUM_FLOWERS; i++)
        // {
        //     flowers[i].x = rand() % SCREEN_WIDTH;
        //     flowers[i].y = SKY_HEIGHT + rand() % (VGA_HEIGHT - SKY_HEIGHT);
        //     flowers[i].color = FLOWER_COLORS[rand() % (sizeof(FLOWER_COLORS) / sizeof(FLOWER_COLORS[0]))];
        // }
    }

    // Update curve parameters
    curve_phase += curve_speed;

    // Update dash offset based on racer velocity
    dash_offset += racer.velocity;
    if (dash_offset >= (dash_length_base + dash_gap_base))
        dash_offset = 0.0f;

    // Draw the road line by line
    for (int y = 479; y >= SKY_HEIGHT; y--)
    {
        // Calculate progression down the road (0.0 at horizon, 1.0 at bottom)
        float progression = (float)(y - SKY_HEIGHT) / (480 - SKY_HEIGHT);

        // Minimum scale factor for thicker road at horizon
        float min_scale = 0.3f;

        // Adjusted perspective scale
        float perspective_scale = min_scale + (1.0f - min_scale) * (progression * progression);

        // Calculate road width at this position
        int road_width = base_road_width * perspective_scale;

        // Calculate curve
        float curve = getRoadCurve((float)y);

        int shift = curve;

        int left_edge = road_center - road_width / 2 - shift;
        int right_edge = road_center + road_width / 2 - shift;

        // Calculate center of road at this y-coordinate (accounting for curve)
        int center_x = (left_edge + right_edge) / 2;

        // Check if this line overlaps with any active obstacles
        bool has_obstacle = false;
        for (int i = 0; i < MAX_OBSTACLES; i++)
        {
            if (obstacles[i].active)
            {
                // Calculate obstacle dimensions based on perspective
                float obs_progression = (float)(obstacles[i].y - SKY_HEIGHT) / (480.0f - SKY_HEIGHT);
                float obs_scale = 0.2f + obs_progression * 2.0f;
                int obs_width = obs_scale * 20;
                int obs_height = obs_scale * 20;

                // Get obstacle boundaries
                int obs_y = obstacles[i].y - obs_height;
                int obs_x = obstacles[i].x - obs_width / 2;

                // Check if this scanline intersects with the obstacle
                if (y >= obs_y && y < obstacles[i].y)
                {
                    has_obstacle = false;
                    break;
                }
            }
        }

        // Selectively erase and redraw only the changed portions
        if (!first_frame)
        {
            // Erase left portion if it's now inside the road
            if (left_edge > prev_left_edges[y])
            {
                drawHLine(prev_left_edges[y], y, left_edge - prev_left_edges[y], ROAD_BACKGROUND);
            }

            // Erase right portion if it's now inside the road
            if (right_edge < prev_right_edges[y])
            {
                drawHLine(right_edge, y, prev_right_edges[y] - right_edge, ROAD_BACKGROUND);
            }

            // Draw left portion if it's now part of the road
            if (left_edge < prev_left_edges[y])
            {
                drawHLine(left_edge, y, prev_left_edges[y] - left_edge, ROAD_COLOR);
            }

            // Draw right portion if it's now part of the road
            if (right_edge > prev_right_edges[y])
            {
                drawHLine(prev_right_edges[y], y, right_edge - prev_right_edges[y], ROAD_COLOR);
            }

            // If there was a dash here previously, erase it first
            if (prev_had_dash[y])
            {
                int half_width = prev_dash_width[y] / 2;
                if (half_width < 1)
                    half_width = 1;
                drawHLine(prev_center_x[y] - half_width, y, prev_dash_width[y], ROAD_COLOR);
            }
        }
        else
        {
            // For first frame, draw the entire line
            drawHLine(left_edge, y, right_edge - left_edge, ROAD_COLOR);
        }

        // Draw center dashed line
        // Calculate dash parameters with perspective
        int dash_width = road_width * dash_width_factor;
        if (dash_width < 1)
            dash_width = 1;

        // Scale dash length and gap based on perspective
        int dash_length = dash_length_base * perspective_scale;
        int dash_gap = dash_gap_base * perspective_scale;

        // Only draw dash if not in a gap area
        float adjusted_y = y + road_scroll;
        int dash_cycle = dash_length + dash_gap;
        int dash_pos = ((int)adjusted_y) % dash_cycle;

        // Track whether this scanline has a dash for next frame
        bool has_dash = dash_pos < dash_length && !has_obstacle;

        if (has_dash)
        {
            // Ensure the dash width is at least 1 pixel
            int half_width = (dash_width / 2);
            if (half_width < 1)
                half_width = 1;

            // Draw the center dash at this y-coordinate
            drawHLine(center_x - half_width, y, dash_width, CENTER_LINE_COLOR);
        }

        // Store current values for next frame
        prev_left_edges[y] = left_edge;
        prev_right_edges[y] = right_edge;
        prev_center_x[y] = center_x;
        prev_dash_width[y] = dash_width;
        prev_had_dash[y] = has_dash;
    }

    // Update road_scroll
    road_scroll -= racer.velocity;
    if (road_scroll < 0.0f)
    {
        road_scroll += 240.0f;
    }

    // No longer first frame
    first_frame = false;
}

void spawn_obstacle()
{
    for (int i = 0; i < MAX_OBSTACLES; i++)
    {
        if (!obstacles[i].active)
        {
            // Calculate road properties at spawn position
            float spawn_progression = (float)(obstacles[i].y - SKY_HEIGHT) / (480.0f - SKY_HEIGHT);
            float min_scale = 0.3f;
            float perspective_scale = min_scale + (1.0f - min_scale) * (spawn_progression * spawn_progression);

            // Calculate the exact curve at this position
            float curve = getRoadCurve((float)obstacles[i].y);

            // Calculate road width and edges at spawn point
            int base_road_width = 450;
            int road_width = base_road_width * perspective_scale;
            int road_center = 320;

            // Start obstacle slightly below horizon for better visibility
            obstacles[i].y = SKY_HEIGHT + 15;

            // Set obstacle position with exact curve offset
            float left = (float)get_rand_32() / (float)UINT32_MAX;
            float offset = ((float)get_rand_32() / UINT32_MAX) * (road_width * 0.8f);
            if (left < 0.5)
            {

                obstacles[i]
                    .x = road_center + curve + offset;
            }
            else
            {
                obstacles[i]
                    .x = road_center + curve - offset;
            }

            // Initialize the tracking variables
            obstacles[i].prev_x = obstacles[i].x;
            obstacles[i].prev_y = obstacles[i].y;
            obstacles[i].prev_scale = 0.2f + spawn_progression * 2.0f;
            obstacles[i].needs_erase = false; // First frame doesn't need erasing

            obstacles[i].active = true;

            float rand = (float)get_rand_32() / (float)UINT32_MAX;

            float p = .33;
            int num = get_rand_32() % 3;

            if (rand < p)
            {
                // Set to false for now
                obstacles[i].o = Non;

                if (num == Speed)
                {
                    obstacles[i].p = Speed;
                    obstacles[i].width = BELL_WIDTH;
                    obstacles[i].height = BELL_HEIGHT;
                }
                else if (num == Invincible)
                {
                    obstacles[i].p = Invincible;
                    obstacles[i].width = DRAGON_WIDTH;
                    obstacles[i].height = DRAGON_HEIGHT;
                }
                else
                {
                    obstacles[i].p = Martha;
                    obstacles[i].width = PUMPKIN_WIDTH;
                    obstacles[i].height = PUMPKIN_HEIGHT;
                }
            }
            else
            {
                // Set false for now
                obstacles[i].p = None;

                if (num == Bear)
                {
                    obstacles[i].o = Bear;
                    obstacles[i].width = BEAR_WIDTH;
                    obstacles[i].height = BEAR_HEIGHT;
                }
                else if (num == Cow)
                { // works!! DO NOT TOUCH
                    obstacles[i].o = Cow;
                    obstacles[i].width = COW_WIDTH;
                    obstacles[i].height = COW_HEIGHT;
                }
                else
                {
                    obstacles[i].o = Banana;
                    obstacles[i].width = BANANA_WIDTH;
                    obstacles[i].height = BANANA_HEIGHT;
                }
            }
            break;
        }
    }
}

void on_pwm_wrap()
{
    pwm_clear_irq(pwm_gpio_to_slice_num(5));

    static int update_counter = 0;

    update_counter++;
    if (update_counter < UPDATE_FACTOR)
    {
        PT_SEM_SIGNAL(pt, &vga_sem);
        return;
    }
    update_counter = 0;

    // Read and low-pass filter potentiometer value
    adc_select_input(0);
    uint16_t raw_val = adc_read();

    static float smooth_val = 0;
    float alpha = 0.05;
    smooth_val = (1.0f - alpha) * smooth_val + alpha * raw_val;
    uint16_t pot_val = (uint16_t)smooth_val;

    if (pot_val > ADC_MAX)
        pot_val = ADC_MAX;

    // Get the road boundaries at the car's y-position
    int road_y = racer.y;
    float progression = (float)(road_y - SKY_HEIGHT) / (480.0f - SKY_HEIGHT);
    float curve = getRoadCurve((float)road_y);

    // Calculate road properties at this y-position (similar to how it's done in draw_dynamic_road)
    float min_scale = 0.3f;
    float perspective_scale = min_scale + (1.0f - min_scale) * (progression * progression);
    int base_road_width = 600; // Match the value used in draw_dynamic_road
    int road_width = base_road_width * perspective_scale;
    int road_center = 320;
    int shift = curve;

    // Calculate left and right road edges
    int left_edge = road_center - road_width / 2 - shift;
    int right_edge = road_center + road_width / 2 - shift;

    // Add margins (larger on the right side)
    int left_margin = 10;
    int right_margin = 30; // Increased right margin to restrict range on the right
    int usable_left = left_edge + left_margin;
    int usable_right = right_edge - right_margin - KART_WIDTH;
    int usable_width = usable_right - usable_left;

    // Map potentiometer value to the dynamic road width
    racer.x = usable_left + ((ADC_MAX - pot_val) * usable_width) / ADC_MAX;

    // Ensure the car stays within the road boundaries
    if (racer.x < usable_left)
        racer.x = usable_left;
    if (racer.x > usable_right)
        racer.x = usable_right;

    // Signal VGA update
    PT_SEM_SIGNAL(pt, &vga_sem);
}

// Does all background logic for stats n sky n such
static PT_THREAD(protothread_background(struct pt *pt))
{
    // Spawn obstacles between this min and max units
    int min_obstacle_spawn = 1000;
    int max_obstacle_spawn = 1500;
    int obstacle_threshold = (get_rand_32() % (max_obstacle_spawn - min_obstacle_spawn)) + min_obstacle_spawn;

    PT_BEGIN(pt);

    init_clouds();
    static int distance_amp_threshold = 0;
    static int distance_lap_tracker = 0;

    static int distance_obstacle_tracker = 0;
    while (1)
    {
        // 1) draw & move clouds
        PT_SEM_WAIT(pt, &vga_sem);

        if (w == Morning)
        {
            for (int i = 0; i < CLOUD_COUNT; i++)
            {
                draw_cloud(&clouds[i], LIGHT_BLUE);
                clouds[i].x += clouds[i].speed;
                if (clouds[i].x > SCREEN_WIDTH)
                    clouds[i].x = -CLOUD_WIDTH; // wrap back to left
                draw_cloud(&clouds[i], WHITE);
            }
        }
        else if (w == Sunset)
        {
            for (int i = 0; i < CLOUD_COUNT; i++)
            {
                draw_cloud(&clouds[i], PINK);
                clouds[i].x += clouds[i].speed;
                if (clouds[i].x > SCREEN_WIDTH)
                    clouds[i].x = -CLOUD_WIDTH; // wrap back to left
                draw_cloud(&clouds[i], LIGHT_PINK);
            }
        }
        else if (w == Night)
        {
            for (int i = 0; i < CLOUD_COUNT; i++)
            {
                draw_cloud(&clouds[i], BLUE);
                clouds[i].x += clouds[i].speed;
                if (clouds[i].x > SCREEN_WIDTH)
                    clouds[i].x = -CLOUD_WIDTH; // wrap back to left
                draw_cloud(&clouds[i], LIGHT_BLUE);
            }
        }

        PT_SEM_SIGNAL(pt, &vga_sem);

        if (distance - distance_obstacle_tracker > obstacle_threshold)
        {
            spawn_obstacle();
            distance_obstacle_tracker = distance;
            obstacle_threshold = (get_rand_32() % (max_obstacle_spawn - min_obstacle_spawn)) + min_obstacle_spawn;
        }

        // Here is how the track goes:

        // 50k+ we straighten the curve
        if (distance >= DIST_STRAIGHT_START && (distance - distance_amp_threshold) > STRAIGHT_CHANGE && amplitude > 0.0f)
        {
            amplitude -= 1.0f;
            distance_amp_threshold = distance;
        }
        // 20k–30k go to apex of curve
        else if (distance >= DIST_MAX_START && distance <= DIST_MAX_END && (distance - distance_amp_threshold) > MAX_CHANGE && amplitude < AMP_MAX_CAP)
        {
            amplitude += 1.0f;
            distance_amp_threshold = distance;
        }
        // 10k–20k we do a gentle curve
        else if (distance > DIST_MILD_START && distance <= DIST_MILD_END && (distance - distance_amp_threshold) > MILD_CHANGE && amplitude < AMP_MILD_CAP)
        {
            amplitude += 1.0f;
            distance_amp_threshold = distance;
        }

        // 70k - 80k do nothing

        // reset, start new lap
        if (distance > LAP_LENGTH)
        {
            distance = 0;
            distance_obstacle_tracker = distance;
            distance_amp_threshold = 0;
            laps += 1;
        }

        if (speed_up)
        {
            speed_duration -= 1;
            if (speed_duration == 0)
            {
                speed_up = false;
            }
        }

        if (invincible)
        {
            invincible_duration -= 1;
            if (invincible_duration == 0)
            {
                invincible = false;
            }
        }

        if (weather_duration > 0)
        {
            weather_duration -= 1;
            if (weather_duration == 0)
            {
                update_background = true;
                w = Morning;
            }
        }
        // 4) pause before next frame
        PT_YIELD_usec(50000); // ~20 Hz
    }

    PT_END(pt);
}

#define VGA_WIDTH 640
#define VGA_HEIGHT 480

static inline void drawPicture(short x, short y, const unsigned short *pic, short width, short height)
{

    for (short i = 0; i < height; i++)
    {
        for (short j = 0; j < width; j++)
        {
            short color = pic[i * width + j];
            if (color != 15)
            {
                drawPixel(x + j, y + i, color);
            }
        }
    }
}

static PT_THREAD(protothread_stats(struct pt *pt))
{

    // Indicate start of thread
    PT_BEGIN(pt);
    static uint64_t prev_acc_time = 0;
    static float prev_distance = -1;
    static int prev_lap = -1;

    static bool first_time = true;

    if (prev_acc_time == 0)
    {
        prev_acc_time = to_us_since_boot(get_absolute_time());
    }

    if (first_time)
    {
        fillRect(0, 0, 640, SKY_HEIGHT, LIGHT_BLUE);

        // Parameter labels
        setTextSize(1);
        setTextColor(WHITE);

        setCursor(10, 10);
        writeString("Time:");

        setCursor(10, 20);
        writeString("Laps:");
        setCursor(10, 30);
        writeString("Distance:");

        fillCircle(550, 40, 30, YELLOW);
        first_time = false;
    }

    while (1)
    {
        PT_SEM_SIGNAL(pt, &vga_sem);
        uint64_t prev_time = to_us_since_boot(get_absolute_time()) / 1000000;

        int new_distance = distance + racer.velocity;

        if (update_background)
        {
            switch (w)
            {
            case Morning:
                // draw sky as before
                fillRect(0, 0, 640, SKY_HEIGHT, LIGHT_BLUE);
                fillCircle(550, 40, 30, YELLOW);
                background_stats = LIGHT_BLUE;
                break;
            case Sunset:
                fillRect(0, 0, 640, SKY_HEIGHT, PINK);
                background_stats = PINK;
                fillCircle(320, 40, 30, ORANGE);
                break;
            case Night:
                fillRect(0, 0, 640, SKY_HEIGHT, BLUE);
                background_stats = BLUE;
                fillCircle(150, 40, 30, WHITE);
                break;
            }

            setTextColor2(WHITE, background_stats);
            setCursor(10, 10);
            writeString("Time:");
            setCursor(75, 10);
            sprintf(display_text, "%d", prev_time);
            writeString(display_text);

            setCursor(10, 20);
            writeString("Laps:");
            setCursor(75, 20);
            sprintf(display_text, "%d/3", laps);
            writeString(display_text);

            setCursor(10, 30);
            writeString("Distance:");
            setCursor(75, 30);
            sprintf(display_text, "%d", prev_time);
            writeString(display_text);

            update_background = false;
        }

        // Set text size
        if (distance != new_distance || update_background)
        {
            setCursor(75, 30);
            setTextColor2(WHITE, background_stats);
            sprintf(display_text, "%d", new_distance + (laps * LAP_LENGTH));
            writeString(display_text);

            distance = new_distance;
        }

        if (prev_time != time || update_background)
        {
            setCursor(75, 10);
            setTextColor2(WHITE, background_stats);
            sprintf(display_text, "%d", prev_time);
            writeString(display_text);

            time = (int)prev_time;
        }

        if (prev_lap != laps || update_background)
        {
            setCursor(75, 20);
            setTextColor2(WHITE, background_stats);
            sprintf(display_text, "%d/3", laps);
            writeString(display_text);

            prev_lap = laps;
        }

        if (laps >= 3)
        {
            // stop updating everything else and show victory
            show_win_screen();
            // lock up here so the screen stays
            while (gpio_get(BUTTON_PIN))
            {
                tight_loop_contents();
            }
        }

        // In protothread_stats function - pedal control implementation

        // Read brake pedal (left pedal - ADC1)
        adc_select_input(POT1_ADC_CH);
        uint16_t raw1 = adc_read();
        float p1_mm = (raw1 / 4095.0f) * POT1_MAX_MM;

        // Apply brake logic - if pedal is pushed (value less than 48)
        if (p1_mm < 48.0f)
        {
            uint64_t acc_time = to_us_since_boot(get_absolute_time());
            // Brake is being applied, gradually reduce velocity
            if (acc_time - prev_acc_time > 125000)
            {
                int new_velocity = racer.velocity - 1;
                if (new_velocity >= MIN_VELOCITY)
                {
                    racer.velocity = new_velocity;
                }
                else
                {
                    racer.velocity = MIN_VELOCITY; // Fully stopped
                }
                prev_acc_time = acc_time;
            }
        }
        // Define this static variable to retain value between calls
        static float filtered_p2_mm = 0.0f;

        // Choose alpha between 0.0f (very slow) and 1.0f (no filter); 0.1–0.3 is common
        const float alpha = 0.1f;

        adc_select_input(POT2_ADC_CH);
        uint16_t raw2 = adc_read();
        float p2_mm = (raw2 / 4095.0f) * POT2_MAX_MM;

        // Apply exponential moving average
        filtered_p2_mm = alpha * p2_mm + (1.0f - alpha) * filtered_p2_mm;

        // Apply accelerator logic - if pedal is pushed (value less than 45)
        if (filtered_p2_mm < 42.0f)
        {
            // Calculate how much the pedal is pushed (lower value = more acceleration)
            uint64_t acc_time = to_us_since_boot(get_absolute_time());

            // Don't accelerate if brake is being applied
            if ((p1_mm >= 48.0f) && (acc_time - prev_acc_time > 125000))
            {
                prev_acc_time = acc_time;
                int new_velocity = racer.velocity + 1;
                if (new_velocity <= MAX_VELOCITY)
                {
                    racer.velocity = new_velocity;
                }
                else
                {
                    racer.velocity = MAX_VELOCITY; // Cap at maximum velocity
                }
            }
        }

        // If neither pedal is pressed, gradually slow down (simulating coasting)
        if (p1_mm >= 49.0f && p2_mm >= 42.0f && racer.velocity > MIN_VELOCITY)
        {
            uint64_t acc_time = to_us_since_boot(get_absolute_time());
            if (acc_time - prev_acc_time > 3000000)
            {
                racer.velocity--;

                prev_acc_time = acc_time;
            }
        }

        PT_SEM_SIGNAL(pt, &vga_sem);

        PT_YIELD_usec(500);
    }
    PT_END(pt);
}

inline void powerUp(Objects *o)
{
    switch (o->p)
    {
    case Martha:
        printf("Martha is matched: change the background");
        if (w == Morning)
            w = Sunset;
        else if (w == Sunset)
            w = Night;
        else if (w == Night)
            w = Morning;
        update_background = true;
        weather_duration = 100;
        break;

    case Speed:
        printf("Speed is matched: increase velocity");
        speed_up = true;
        racer.velocity = MAX_VELOCITY;
        speed_duration = 500;
        break;

    case Invincible:
        printf("Invincible is matched: anything is possible!!!");
        racer.invincible = true;
        invincible = true;
        invincible_duration = 100;
        break;
    default:
        break;
    }
}

inline void bummpy(Objects *o)
{
    switch (o->o)
    {

    // In your collision handler
    case Cow:
        printf("Cow is matched: make the moo sound ");
        o->y -= 75;
        distance -= 5;
        break;

    case Bear:
        printf("Bear is matched: make a sad bear sound ");
        o->y -= 50;
        distance -= 10;
        break;

    case Banana:
        printf("Banana is matched: make a banana sound 'Slippery when peeled.'");
        o->y -= 15;
        distance -= 15;
        break;

    default:
        break;
    }
}

void draw_kart_sprite(int x, int y, int s)
{
    fillRect(x + 1 * s, y + 3 * s, 3 * s, 14 * s, WHITE);  // Left tire
    fillRect(x + 16 * s, y + 3 * s, 3 * s, 14 * s, WHITE); // Right tire
    fillRect(x + 4 * s, y + 2 * s, 12 * s, 16 * s, RED);
    fillRect(x + 6 * s, y + 2 * s, 8 * s, 2 * s, RED);
    fillRect(x + 6 * s, y + 16 * s, 8 * s, 2 * s, RED);
    fillRect(x + 6 * s, y + 5 * s, 8 * s, 3 * s, BLACK);
    fillRect(x + 6 * s, y + 8 * s, 8 * s, 3 * s, WHITE);
    fillRect(x + 6 * s, y + 11 * s, 8 * s, 3 * s, WHITE);
}

void erase_kart_sprite(int x, int y, int scale)
{
    fillRect(x, y, 21 * scale, 21 * scale, BLACK);
}

static PT_THREAD(protothread_vga(struct pt *pt))
{
    PT_BEGIN(pt);

    // fill whole play area with grass, not black
    // fill every pixel below the sky with grass:
    fillRect(0, SKY_HEIGHT,
             SCREEN_WIDTH, // 640
             VGA_HEIGHT - SKY_HEIGHT,
             GRASS_COLOR);

    drawPicture(RIGHT_TREE_X + 30, TREE_Y + 60, vga_image_tree, TREE_WIDTH, TREE_HEIGHT);

    while (1)
    {
        PT_SEM_WAIT(pt, &vga_sem);

        // 2) Redraw the road per scanline
        draw_dynamic_road(racer.x);

        drawPicture(LEFT_TREE_X, TREE_Y, vga_image_tree, TREE_WIDTH, TREE_HEIGHT);

        // 5) Update & draw active obstacles
        for (int i = 0; i < MAX_OBSTACLES; i++)
        {
            // Skip obstacle if not active
            if (!obstacles[i].active)
                continue;

            bool is_obstacle = obstacles[i].o != None;
            int object_type;
            if (is_obstacle)
            {
                object_type = obstacles[i].o;
            }
            else
            {
                object_type = obstacles[i].p;
            }

            // Erase obj from previous position if needed
            if (obstacles[i].needs_erase)
            {
                float prev_progression = (float)(obstacles[i].prev_y - SKY_HEIGHT) / (480.0f - SKY_HEIGHT);

                int prev_obj_width = obstacles[i].prev_scale * obstacles[i].width;
                int prev_obj_height = obstacles[i].prev_scale * 20;

                // Calculate the area to erase (obj's previous bounding box)
                float prev_obj_x = obstacles[i].x + prev_obj_width / 2;
                float scale = 0.2f + prev_progression * 2.0f;

                drawObject(prev_obj_x,
                           obstacles[i].y,
                           is_obstacle,
                           object_type,
                           obstacles[i].prev_scale, true);
            }

            // Save current position before updating
            obstacles[i].prev_x = obstacles[i].x;
            obstacles[i].prev_y = obstacles[i].y;
            obstacles[i].prev_scale = 0.2f + ((float)(obstacles[i].y - SKY_HEIGHT) / (480.0f - SKY_HEIGHT)) * 2.0f;
            obstacles[i].needs_erase = true;

            // Calculate road properties at this y position
            float progression = (float)(obstacles[i].y - SKY_HEIGHT) / (480.0f - SKY_HEIGHT);

            // Use the same perspective calculation as the road drawing
            float min_scale = 0.3f;
            float perspective_scale = min_scale + (1.0f - min_scale) * (progression * progression);

            // Calculate road width at this position - now using the wider base width
            int base_road_width = 450;
            int road_width = base_road_width * perspective_scale;

            // Calculate road curve at this position
            float curve = getRoadCurve((float)obstacles[i].y);

            // Calculate road edges
            int road_center = 320;
            int road_left = road_center - road_width / 2 - curve;
            int road_right = road_center + road_width / 2 - curve;

            // Move obstacle down the road
            obstacles[i].y += racer.velocity;

            // Apply some obstacle movement based on the road curve (less aggressive)
            obstacles[i].x += curve * 0.005f; // Reduced from 0.01f to 0.005f

            // Calculate obj size based on perspective
            float scale = 0.2f + progression * 2.0f;
            int obj_width = scale * obstacles[i].width;
            int obj_height = scale * obstacles[i].height;

            float usable_road_width = road_width * 0.4f;
            // Center point of usable road area
            float road_center_shifted = road_center - curve;
            // Calculate absolute boundaries
            float left_boundary = road_center_shifted - usable_road_width;
            float right_boundary = road_center_shifted + usable_road_width;

            // Keep obj center point within these boundaries with additional padding
            float padding = obj_width * 0.1f; // 10% padding

            float min_x = left_boundary + obj_width / 2 + padding;
            float max_x = right_boundary - obj_width / 2 - padding;

            // drawRect(left_boundary, obstacles[i].y, right_boundary - left_boundary, 5, BLUE);

            if (obstacles[i].x < min_x)
                obstacles[i].x = min_x;
            else if (obstacles[i].x > max_x)
                obstacles[i].x = max_x;

            // Calculate drawing position (cow's top-left corner)
            float obj_x = obstacles[i].x + obj_width / 2;

            // Check for collision with kart
            float obj_y = obstacles[i].y - obj_height;

            if (obstacles[i].y > 350 &&
                collision(obj_x, obj_width, obstacles[i].y, obj_height))
            {
                if (obstacles[i].p == Non)
                {
                    bummpy(&obstacles[i]);
                    // TODO: CHANGE THIS BACK TO MAX(0, RACER.VELOCITY - 2) AFTER WE ARE DONE DEBUGGING - ELIAS
                    racer.velocity = max(MIN_VELOCITY, min(0, MAX_VELOCITY));
                }
                else
                {
                    powerUp(&obstacles[i]);
                    obstacles[i].active = false;
                }

                continue;
            }

            // Remove obstacles that go off-screen
            if (obstacles[i].y > VGA_HEIGHT)
            {
                // now fully deactivate
                drawObject(
                    obstacles[i].prev_x,
                    obstacles[i].prev_y,
                    is_obstacle,
                    object_type,
                    obstacles[i].prev_scale, true);
                obstacles[i].active = false;
            }
            else
            {
                // drawRect(obj_x, obstacles[i].y, obj_width, obj_height, RED);
                drawObject(obj_x,
                           obstacles[i].y,
                           is_obstacle,
                           object_type,
                           scale, false);
            }
        }

        erase_kart_sprite(prev_kart_x, racer.y, 2);
        draw_kart_sprite(racer.x, racer.y, 2);
        for (int i = 0; i < NUM_FLOWERS; i++)
        {
            int fy = flowers[i].y;
            int fx = flowers[i].x;
            int left = prev_left_edges[fy];
            int right = prev_right_edges[fy];

            if (fx < left || fx > right)
            {
                int half = FLOWER_SIZE / 2;
                drawVLine(fx, fy - half, FLOWER_SIZE, flowers[i].color);
                drawHLine(fx - half, fy, FLOWER_SIZE, flowers[i].color);
            }
        }

        // remember for next frame
        prev_kart_x = racer.x;

        PT_SEM_SIGNAL(pt, &vga_sem);

        PT_YIELD_usec(25000);
    }

    PT_END(pt);
}

void write_dac(uint16_t data, uint16_t channel_bits)
{
    uint16_t dac_word = channel_bits | (data & 0x0FFF);
    gpio_put(PIN_CS, 0); // select the DAC
    spi_write16_blocking(SPI_PORT, &dac_word, 1);
    gpio_put(PIN_CS, 1); // deselect
    // now pulse LDAC to latch
    gpio_put(LDAC, 1);
    gpio_put(LDAC, 0);
}

static PT_THREAD(protothread_sfx(struct pt *pt))
{
    static int index = 0;
    PT_BEGIN(pt);

    while (1)
    {
        write_dac(sfx[index] & 0x0FFF, DAC_CONFIG_CHAN_A); // Output 12-bit sample
        index++;

        if (index >= sfx_len)
            index = 0; // Loop back to start

        PT_YIELD_usec(12); // ~44.1 kHz playback rate
    }

    PT_END(pt);
}

// Initializes SPI, DMA channels, and builds the sine table for DAC output.
void initSPI()
{
    // Initialize SPI channel (channel, baud rate set to 20MHz)
    spi_init(SPI_PORT, 20000000);

    // Format SPI channel (channel, data bits per transfer, polarity, phase, order)
    spi_set_format(SPI_PORT, 16, 0, 0, 0);

    // Map SPI signals to GPIO ports, acts like framed SPI with this CS mapping
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    // Build sine table and DAC data table
    int i;
    for (i = 0; i < (SINE_TABLE_SIZE); i++)
    {
        raw_sin[i] = (int)(2047 * sin((float)i * 6.283 / (float)SINE_TABLE_SIZE) + 2047); // 12 bit
        DAC_data[i] = DAC_CONFIG_CHAN_A | (raw_sin[i] & 0x0fff);
    }

    // Select DMA channels
    data_channel = dma_claim_unused_channel(true);
    control_channel = dma_claim_unused_channel(true);

    // Setup the control channel
    dma_channel_config c = dma_channel_get_default_config(control_channel); // default configs
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);                 // 32-bit txfers
    channel_config_set_read_increment(&c, false);                           // no read incrementing
    channel_config_set_write_increment(&c, false);                          // no write incrementing
    channel_config_set_chain_to(&c, data_channel);                          // chain to data channel

    dma_channel_configure(
        control_channel,                     // Channel to be configured
        &c,                                  // The configuration we just created
        &dma_hw->ch[data_channel].read_addr, // Write address (data channel read address)
        &dac_data_pointer,                   // Read address (POINTER TO AN ADDRESS)
        1,                                   // Number of transfers
        false                                // Don't start immediately
    );

    // Setup the data channel
    dma_channel_config c2 = dma_channel_get_default_config(data_channel); // Default configs
    channel_config_set_transfer_data_size(&c2, DMA_SIZE_16);              // 16-bit txfers
    channel_config_set_read_increment(&c2, true);                         // yes read incrementing
    channel_config_set_write_increment(&c2, false);                       // no write incrementing

    // (X/Y)*sys_clk, where X is the first 16 bytes and Y is the second
    // sys_clk is 125 MHz unless changed in code. Configured to ~44 kHz
    dma_timer_set_fraction(0, 0x0017, 0xffff);

    // 0x3b means timer0 (see SDK manual)
    channel_config_set_dreq(&c2, 0x3b); // DREQ paced by timer 0

    // chain to the controller DMA channel
    dma_channel_configure(
        data_channel,              // Channel to be configured
        &c2,                       // The configuration we just created
        &spi_get_hw(SPI_PORT)->dr, // write address (SPI data register)
        DAC_data,                  // The initial read address
        SINE_TABLE_SIZE,           // Number of transfers
        false                      // Don't start immediately.
    );
}

void core1_entry()
{
    pt_add_thread(protothread_vga);
    pt_add_thread(protothread_stats);
    pt_add_thread(protothread_background);
    pt_schedule_start;
}

int main()
{
    stdio_init_all();
    initVGA();
    i2c_init(I2C_CHAN, I2C_BAUD_RATE);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_set_function(5, GPIO_FUNC_PWM);
    gpio_set_function(4, GPIO_FUNC_PWM);
    pwm_slice = pwm_gpio_to_slice_num(5);

    pwm_clear_irq(pwm_slice);
    pwm_set_irq_enabled(pwm_slice, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
    irq_set_enabled(PWM_IRQ_WRAP, true);
    pwm_set_wrap(pwm_slice, WRAPVAL);
    pwm_set_clkdiv(pwm_slice, CLKDIV);
    pwm_set_chan_level(pwm_slice, PWM_CHAN_B, 0);
    pwm_set_chan_level(pwm_slice, PWM_CHAN_A, 0);
    pwm_set_mask_enabled((1u << pwm_slice));
    pwm_set_output_polarity(pwm_slice, 1, 0);

    // Setup GPIOs for other peripherals
    gpio_init(10);
    gpio_set_dir(10, GPIO_OUT);
    gpio_put(10, 1);

    gpio_init(13);
    gpio_set_dir(13, GPIO_IN);

    adc_init();
    adc_gpio_init(26);
    adc_select_input(0);
    adc_gpio_init(27); // ADC1 on GPIO27
    adc_gpio_init(28); // ADC2 on GPIO28

    int height = 480;
    int width = 640;
    for (short i = 0; i < height; i++)
    {
        for (short j = 0; j < width; j++)
        {
            uint8_t index = vga_image_intro[i * width + j] & 0x0F;
            drawPixel(j, i, index);
        }
    }

    initSPI();

    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    sleep_ms(10);
    while (gpio_get(BUTTON_PIN))
    {
        distance = 0;
        time = 0;
    };

    multicore_reset_core1();
    multicore_launch_core1(core1_entry);

    pt_add_thread(protothread_sfx);
    pt_schedule_start;
}
