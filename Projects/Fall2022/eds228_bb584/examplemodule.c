// This is a C module that contains code that can be called from micropython

// Include MicroPython API.
#include "py/runtime.h"
// Used to get the time in the Timer class example.
#include "py/mphal.h"

// 4760 code added to this file
#if 1
// pico-sdk files are included in the micropython repo
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "hardware/spi.h"

// Macros for fixed-point arithmetic (faster than floating point)
typedef signed int fix15;
#define multfix15(a, b) ((fix15)((((signed long long)(a)) * ((signed long long)(b))) >> 15))
#define float2fix15(a) ((fix15)((a)*32768.0))
#define fix2float15(a) ((float)(a) / 32768.0)
#define absfix15(a) abs(a)
#define int2fix15(a) ((fix15)(a << 15))
#define fix2int15(a) ((int)(a >> 15))
#define char2fix15(a) (fix15)(((fix15)(a)) << 15)
#define divfix(a, b) (fix15)((((signed long long)(a)) << 15) / (b))

// Direct Digital Synthesis (DDS) parameters
#define two32 4294967296.0 // 2^32 (a constant)
#define Fs 40000           // sample rate

// the DDS units - core 1
// Phase accumulator and phase increment. Increment sets output frequency.
volatile unsigned int phase_accum_main_1;
volatile unsigned int phase_incr_main_1 = (1400.0 * two32) / Fs;
// the DDS units - core 2
// Phase accumulator and phase increment. Increment sets output frequency.
volatile unsigned int phase_accum_main_0;
volatile unsigned int phase_incr_main_0 = (1400.0 * two32) / Fs;

// DDS sine table (populated in main())
#define sine_table_size 256
fix15 sin_table[sine_table_size];

// Values output to DAC
int DAC_output_0;
int DAC_output_1;

// Amplitude modulation parameters and variables
fix15 max_amplitude = int2fix15(1); // maximum amplitude
fix15 attack_inc;                   // rate at which sound ramps up
fix15 decay_inc;                    // rate at which sound ramps down
fix15 current_amplitude_0 = 0;      // current amplitude (modified in ISR)
fix15 current_amplitude_1 = 0;      // current amplitude (modified in ISR)

// Timing parameters for beeps (units of interrupts)
#define ATTACK_TIME 200
#define DECAY_TIME 200
#define SUSTAIN_TIME 10000
#define BEEP_DURATION 1000
#define BEEP_REPEAT_INTERVAL 680
#define NUM_SYLLABLES 8
#define CHIRP_REPEAT_INTERVAL 15000

// State machine variables
enum State
{
    SYLL = 0,
    SYLL_PAUSE = 1,
    CHIRP_PAUSE = 2
};
volatile enum State STATE_0 = SYLL;
volatile unsigned int count_0 = 0;
volatile unsigned int syll_count_0 = 0;
volatile enum State STATE_1 = SYLL;
volatile unsigned int count_1 = 0;
volatile unsigned int syll_count_1 = 0;

// SPI data
uint16_t DAC_data_1; // output value
uint16_t DAC_data_0; // output value

// DAC parameters (see the DAC datasheet)
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

// SPI configurations (note these represent GPIO number, NOT pin number)
#define PIN_MISO 16 // 21
#define PIN_CS 17   // 22
#define PIN_SCK 18  // 24
#define PIN_MOSI 19 // 25
#define LDAC 20     // 26
#define LED 25
#define SPI_PORT spi0

// Two variables to store core number
volatile int corenum_0;
volatile int corenum_1;

// Create a repeating timer that calls repeating_timer_callback.
static struct repeating_timer timer_core_1;

int duration = 0;
// This timer ISR is called on core 1
bool repeating_timer_callback_core_1(struct repeating_timer *t)
{
    // a faint LED should appear to indicate the ISR is running
    count_1 % 2 == 0 ? gpio_put(LED, 1) : gpio_put(LED, 0);

    // only play alarm for a few seconds
    if (duration <= 0) {
        current_amplitude_1 = 0;
    }
    else {
        duration -= 1;
    }

    // button pressed?
    count_1 += 1;
    if (STATE_1 == SYLL)
    {
        // DDS phase and sine table lookup
        phase_accum_main_1 += phase_incr_main_1;

        DAC_output_1 = fix2int15(multfix15(current_amplitude_1,
                                           sin_table[phase_accum_main_1 >> 24])) +
                       2048;
        // Ramp up amplitude
        if (count_1 < ATTACK_TIME)
        {
            current_amplitude_1 = (current_amplitude_1 + attack_inc);
        }
        // Ramp down amplitude
        else if (count_1 > BEEP_DURATION - DECAY_TIME)
        {
            current_amplitude_1 = (current_amplitude_1 - decay_inc);
        }

        // Mask with DAC control bits
        DAC_data_1 = (DAC_config_chan_A | (DAC_output_1 & 0xffff));

        // SPI write (no spinlock b/c of SPI buffer)
        spi_write16_blocking(SPI_PORT, &DAC_data_1, 1);

        // State transition?
        if (count_1 == BEEP_DURATION)
        {
            count_1 = 0;
            syll_count_1++;
            if (syll_count_1 == NUM_SYLLABLES)
            {
                STATE_1 = CHIRP_PAUSE;
                syll_count_1 = 0;
            }
            else
            {
                STATE_1 = SYLL_PAUSE;
            }
        }
    }

    // State transition?
    else if (STATE_1 == SYLL_PAUSE)
    {

        if (count_1 == BEEP_REPEAT_INTERVAL)
        {
            current_amplitude_1 = 0;
            STATE_1 = SYLL;
            count_1 = 0;
        }
    }

    else if (STATE_1 == CHIRP_PAUSE)
    {

        if (count_1 == CHIRP_REPEAT_INTERVAL)
        {
            current_amplitude_1 = 0;
            STATE_1 = SYLL;
            count_1 = 0;
        }
    }

    // retrieve core number of execution
    corenum_1 = get_core_num();

    return true;
}

#if 0
// This timer ISR is called on core 0
bool repeating_timer_callback_core_0(struct repeating_timer* t)
{
    return true;
        count_0 += 1;
        if (STATE_0 == SYLL)
        {
            // DDS phase and sine table lookup
            phase_accum_main_0 += phase_incr_main_0;
            DAC_output_0 = fix2int15(multfix15(current_amplitude_0,
                sin_table[phase_accum_main_0 >> 24])) +
                2048;

            // Ramp up amplitude
            if (count_0 < ATTACK_TIME)
            {
                current_amplitude_0 = (current_amplitude_0 + attack_inc);
            }
            // Ramp down amplitude
            else if (count_0 > BEEP_DURATION - DECAY_TIME)
            {
                current_amplitude_0 = (current_amplitude_0 - decay_inc);
            }

            // Mask with DAC control bits
            DAC_data_0 = (DAC_config_chan_B | (DAC_output_0 & 0xffff));

            // SPI write (no spinlock b/c of SPI buffer)
            spi_write16_blocking(SPI_PORT, &DAC_data_0, 1);

            // Increment the counter

            if (count_0 == BEEP_DURATION)
            {
                count_0 = 0;
                syll_count_0++;
                if (syll_count_0 == NUM_SYLLABLES)
                {
                    STATE_0 = CHIRP_PAUSE;
                    syll_count_0 = 0;
                }
                else
                {
                    STATE_0 = SYLL_PAUSE;
                }
            }
        }

        // State transition?

        else if (STATE_0 == SYLL_PAUSE)
        {

            if (count_0 == BEEP_REPEAT_INTERVAL)
            {
                current_amplitude_0 = 0;
                STATE_0 = SYLL;
                count_0 = 0;
            }
        }

        else if (STATE_0 == CHIRP_PAUSE)
        {

            if (count_0 == CHIRP_REPEAT_INTERVAL)
            {
                current_amplitude_0 = 0;
                STATE_0 = SYLL;
                count_0 = 0;
            }
        }
    
    // retrieve core number of execution
    corenum_0 = get_core_num();

    return true;
}

#endif

// This is the core 1 entry point. Essentially main() for core 1
void core1_entry()
{

    // create an alarm pool on core 1
    alarm_pool_t *core1pool;
    core1pool = alarm_pool_create(2, 16);

    // Negative delay so means we will call repeating_timer_callback, and call it
    // again 25us (40kHz) later regardless of how long the callback took to execute
    alarm_pool_add_repeating_timer_us(core1pool, -25,
                                      repeating_timer_callback_core_1, NULL, &timer_core_1);

}

// Core 0 entry point
int init()
{
    // Initialize SPI channel (channel, baud rate set to 20MHz)
    spi_init(SPI_PORT, 20000000);
    // Format (channel, data bits per transfer, polarity, phase, order)
    spi_set_format(SPI_PORT, 16, 0, 0, 0);

    // Map SPI signals to GPIO ports
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI);

    // Map LDAC pin to GPIO port, hold it low (could alternatively tie to GND)
    gpio_init(LDAC);
    gpio_set_dir(LDAC, GPIO_OUT);
    gpio_put(LDAC, 0);

    // // Map LED to GPIO port, make it low
    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);
    gpio_put(LED, 0);

    // set up increments for calculating bow envelope
    attack_inc = divfix(max_amplitude, int2fix15(ATTACK_TIME));
    decay_inc = divfix(max_amplitude, int2fix15(DECAY_TIME));

    // Build the sine lookup table
    // scaled to produce values between 0 and 4096 (for 12-bit DAC)
    int ii;
    for (ii = 0; ii < sine_table_size; ii++)
    {
        sin_table[ii] = float2fix15(2047 * sin((float)ii * 6.283 / (float)sine_table_size));
    }
    // Launch core 1
    multicore_launch_core1(core1_entry);

    // Create a repeating timer that calls
    // repeating_timer_callback (defaults core 0)
    // struct repeating_timer timer_core_0;

    // Negative delay so means we will call repeating_timer_callback, and call it
    // again 25us (40kHz) later regardless of how long the callback took to execute
    // add_repeating_timer_us(-25, repeating_timer_callback_core_0, NULL, &timer_core_0);


    // don't return
    while (true) {

    }
    return 0;
}
#endif

// This is the function which will be called from Python as cexample.add_ints(a, b).
STATIC mp_obj_t example_add_ints(mp_obj_t a_obj, mp_obj_t b_obj)
{
    duration = Fs * 4; // may cause race condition with interrupts

    // Extract the ints from the micropython input objects.
    int a = mp_obj_get_int(a_obj);
    int b = mp_obj_get_int(b_obj);

    // Calculate the addition and convert to MicroPython object.
    return mp_obj_new_int(a + b);
}
// Define a Python reference to the function above.
STATIC MP_DEFINE_CONST_FUN_OBJ_2(example_add_ints_obj, example_add_ints);

// This structure represents Timer instance objects.
typedef struct _example_Timer_obj_t
{
    // All objects start with the base.
    mp_obj_base_t base;
    // Everything below can be thought of as instance attributes, but they
    // cannot be accessed by MicroPython code directly. In this example we
    // store the time at which the object was created.
    mp_uint_t start_time;
} example_Timer_obj_t;

// This is the Timer.time() method. After creating a Timer object, this
// can be called to get the time elapsed since creating the Timer.
STATIC mp_obj_t example_Timer_time(mp_obj_t self_in)
{
    // The first argument is self. It is cast to the *example_Timer_obj_t
    // type so we can read its attributes.
    example_Timer_obj_t *self = MP_OBJ_TO_PTR(self_in);

    // Get the elapsed time and return it as a MicroPython integer.
    mp_uint_t elapsed = mp_hal_ticks_ms() - self->start_time;
    return mp_obj_new_int_from_uint(elapsed);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(example_Timer_time_obj, example_Timer_time);

// This represents Timer.__new__ and Timer.__init__, which is called when
// the user instantiates a Timer object.
STATIC mp_obj_t example_Timer_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args)
{
    // initialize the DDS
    init();

    // Allocates the new object and sets the type.
    example_Timer_obj_t *self = m_new_obj(example_Timer_obj_t);
    self->base.type = (mp_obj_type_t *)type;

    // Initializes the time for this Timer instance.
    self->start_time = mp_hal_ticks_ms();

    // The make_new function always returns self.
    return MP_OBJ_FROM_PTR(self);
}

// This collects all methods and other static class attributes of the Timer.
// The table structure is similar to the module table, as detailed below.
STATIC const mp_rom_map_elem_t example_Timer_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR_time), MP_ROM_PTR(&example_Timer_time_obj)},
};
STATIC MP_DEFINE_CONST_DICT(example_Timer_locals_dict, example_Timer_locals_dict_table);

// This defines the type(Timer) object.
MP_DEFINE_CONST_OBJ_TYPE(
    example_type_Timer,
    MP_QSTR_Timer,
    MP_TYPE_FLAG_NONE,
    make_new, example_Timer_make_new,
    locals_dict, &example_Timer_locals_dict);

// Define all properties of the module.
// Table entries are key/value pairs of the attribute name (a string)
// and the MicroPython object reference.
// All identifiers and strings are written as MP_QSTR_xxx and will be
// optimized to word-sized integers by the build system (interned strings).
STATIC const mp_rom_map_elem_t example_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_cexample)},
    {MP_ROM_QSTR(MP_QSTR_add_ints), MP_ROM_PTR(&example_add_ints_obj)},
    {MP_ROM_QSTR(MP_QSTR_Timer), MP_ROM_PTR(&example_type_Timer)},
};
STATIC MP_DEFINE_CONST_DICT(example_module_globals, example_module_globals_table);

// Define module object.
const mp_obj_module_t example_user_cmodule = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&example_module_globals,
};

// Register the module to make it available in Python.
MP_REGISTER_MODULE(MP_QSTR_cexample, example_user_cmodule);
