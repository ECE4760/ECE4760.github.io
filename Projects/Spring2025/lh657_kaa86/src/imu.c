/**
 * Lina Hao (lh657@cornell.edu), Kotey Ashie (kaa86@cornell.edu)
 *
 * Hardware Connections
 * GPIO 8 --> MPU6050 SDA
 * GPIO 9 --> MPU6050 SCL
 * 3.3v --> MPUG050 VCC
 * RP2040 GND --> MPU6050 GND
 * GPIO 4 --> PWM Output
 * GPIO 13 --> Servo Motor Output
 */

// Standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
// PICO libraries
#include "pico/stdlib.h"
#include "pico/multicore.h"
// Hardware libraries
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/adc.h"
#include "hardware/pio.h"
#include "hardware/i2c.h"
// Custom libraries
#include "mpu6050.h"
#include "pt_cornell_rp2040_v1_3.h"

// GPIO used for PWM output
#define PWM_OUT 4
#define PWM_OUT_2 13

// Parameters for PWM
#define WRAPVAL 5000
#define CLKDIV 25.0f
uint slice_num;
uint slice_num_2;

// LED is connected to GPIO 25
#define LED_PIN 25

// Arrays in which raw measurements will be stored
fix15 acceleration[3], gyro[3];

float latest_accel = 0;

// Flag indicating whether the IMU detected movement from the user
volatile bool motion_detected = false;

// Flag indicating whether the wait period for motion detection has ended
volatile bool wait_ended = true;

void on_pwm_wrap()
{
  // Clear the interrupt flag that brought us here

  pwm_clear_irq(pwm_gpio_to_slice_num(PWM_OUT));
  pwm_clear_irq(pwm_gpio_to_slice_num(PWM_OUT_2));

  // Read IMU and gather measurements
  mpu6050_read_raw(acceleration, gyro);

  int control = 5000;
  float accel = fix2float15(acceleration[1]); // Get y-axis acceleration
  latest_accel = accel;
  int count = 0;

  if (accel >= 1.0 && wait_ended) // Check whether there has been significant motion and wait period is over
  {
    // Turn on LED as visual indicator
    gpio_put(LED_PIN, 1);
    // Set duty cycles for motors
    pwm_set_chan_level(slice_num, PWM_CHAN_A, control);
    pwm_set_chan_level(slice_num_2, PWM_CHAN_B, 2000);
    //  Indicate motion has been detected
    motion_detected = true;
    // Indicate wait period before next motion detection begins
    wait_ended = false;
  }
}

// Thread for motor control
static PT_THREAD(protothread_motor(struct pt *pt))
{
  PT_BEGIN(pt);
  while (1)
  {
    PT_YIELD_UNTIL(pt, motion_detected);
    PT_YIELD_usec(200000); // Let motors spin for 0.2 seconds
    gpio_put(LED_PIN, 0);  // Turn off LED
    // Set duty cycles to 0
    pwm_set_chan_level(slice_num, PWM_CHAN_A, 0);
    pwm_set_chan_level(slice_num_2, PWM_CHAN_B, 0);
    motion_detected = false;
    // Begin 2s cooldown period for motion detection
    PT_YIELD_usec(2000000);
    // Indicate wait period for motion detection has ended
    wait_ended = true;
  }
  PT_END(pt);
}

int main()
{
  // Initialize stdio
  stdio_init_all();

  ////////////////////////////////////////////////////////////////////////
  ///////////////////////// I2C CONFIGURATION ////////////////////////////
  i2c_init(I2C_CHAN, I2C_BAUD_RATE);
  gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);

  // Initialize MPU6050
  mpu6050_reset();
  mpu6050_read_raw(acceleration, gyro);

  // PWM configurations
  // Tell GPIO PWM_OUT that it is allocated to the PWM
  gpio_set_function(PWM_OUT, GPIO_FUNC_PWM);

  gpio_set_function(PWM_OUT_2, GPIO_FUNC_PWM);

  // Initialize LED pin
  gpio_init(LED_PIN);

  // Configure LED pin as output
  gpio_set_dir(LED_PIN, GPIO_OUT);

  // Find out which PWM slice is connected to GPIO PWM_OUT (it's slice 2)
  slice_num = pwm_gpio_to_slice_num(PWM_OUT);

  slice_num_2 = pwm_gpio_to_slice_num(PWM_OUT_2);

  // Mask our slice's IRQ output into the PWM block's single interrupt line,
  // and register our interrupt handler
  pwm_clear_irq(slice_num);
  pwm_set_irq_enabled(slice_num, true);

  pwm_clear_irq(slice_num_2);
  pwm_set_irq_enabled(slice_num_2, true);

  irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
  irq_set_enabled(PWM_IRQ_WRAP, true);

  // This section configures the period of the PWM signals
  pwm_set_wrap(slice_num, WRAPVAL);
  pwm_set_clkdiv(slice_num, CLKDIV);

  pwm_set_wrap(slice_num_2, 10000);
  pwm_set_clkdiv(slice_num_2, 250.0f);

  // Invert?
  // First argument is slice number.
  // If second argument is true, channel A is inverted.
  // If third argument is true, channel B is inverted.
  pwm_set_output_polarity(slice_num, 1, 0);

  pwm_set_output_polarity(slice_num_2, 0, 0);

  // This sets duty cycle
  pwm_set_chan_level(slice_num, PWM_CHAN_B, 0);
  pwm_set_chan_level(slice_num, PWM_CHAN_A, 0);

  pwm_set_chan_level(slice_num_2, PWM_CHAN_B, 0);
  pwm_set_chan_level(slice_num_2, PWM_CHAN_A, 0);

  // Start the channel
  pwm_set_mask_enabled((1u << slice_num) | (1u << slice_num_2));

  // Start core 0
  // pt_add_thread(protothread_serial);
  pt_add_thread(protothread_motor);
  pt_schedule_start;
}