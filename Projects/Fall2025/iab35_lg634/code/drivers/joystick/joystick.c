/*
This file is part of peromyscus.

peromyscus is free software: you can redistribute it and/or modify it under
the terms of the GNU Affero General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option) any
later version.

peromyscus is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
details.

You should have received a copy of the GNU Affero General Public License along
with peromyscus. If not, see <https://www.gnu.org/licenses/>.
*/
#include "joystick.h"
#include <hardware/adc.h>
#include <hardware/clocks.h>
#include <hardware/irq.h>

#include "../../global.h"

#include <stdckdint.h>
#include <stdlib.h>

// previous values for filtering
static int32_t old_x, old_y = 0;

// ADC IRQ handler
void joystick_handler(){
  union sensor_data out;
  // get the new values from FIFO and take only stable bits: 0 to 255 each effective
  int32_t new_x = (adc_fifo_get() >> 3);
  int32_t new_y = (adc_fifo_get() >> 3);

  // low pass filtering with checked arithmetic
  // we don't particularly care about whether overflows happen, just that the numbers are truncated right
  int32_t tmpx, tmpy;
  ckd_sub(&tmpx, new_x, old_x);
  ckd_sub(&tmpy, new_y, old_y);

  ckd_add(&old_x, old_x, (tmpx / 32));
  ckd_add(&old_y, old_y, (tmpy / 32));

  ckd_sub(&out.delta_x, old_x, joystick_config.centre_x);
  ckd_sub(&out.delta_y, old_y, joystick_config.centre_y);

  // scale the deltas
  out.delta_x /= 16;
  out.delta_y /= 16;
 

  // clamp small delta values to zero
  if (abs(out.delta_x) < 2){
     out.delta_x = 0;
  }
   if (abs(out.delta_y) < 2){
     out.delta_y = 0;
   }

  // create and populate the driver_event_t
  struct driver_event_t * data = equeue_alloc(&workqueue,event_size);
  data->data = out.raw;
  data->id.source = from_joystick;
  data->id.pin = joystick_config.x_pin;
  data->id.misc = 0;

  // post the handler with driver data.
  equeue_post(&workqueue,joystick_config.handler,data);
}

// check whether a pin can be used for ADC.
static inline bool adc_pin_ok(pin_no pin){
  return (26 <= pin) && (pin <= 29);
}

// initialise hardware
bool init_joystick(){
  // check if pins can be used for ADC
  if (!adc_pin_ok(joystick_config.x_pin) || !adc_pin_ok(joystick_config.y_pin)){
    return false;
  }
  // check that pins are different
  if (joystick_config.x_pin == joystick_config.y_pin){
    return false;
  }

  // init GPIO
  adc_init();
  adc_gpio_init(joystick_config.x_pin);
  adc_gpio_init(joystick_config.y_pin);

  // create the round robin mask and apply it
  uint8_t adc_mask = (1 << (joystick_config.x_pin - 26)) | (1 << (joystick_config.y_pin - 26));

  adc_set_round_robin(adc_mask);

  // set the divider to get the desired frequency.
  // the times two is because the pins are read in sequence
  float target_div = (float)clock_get_hz(clk_sys) / (float)(joystick_config.poll_rate * 2);
  adc_set_clkdiv(target_div);

  // set FIFO to 2 entries, and interrupt on full, no DMA stuff
  adc_fifo_setup(true,false,2,false,false);

  // add as handler.
  irq_set_exclusive_handler(ADC_IRQ_FIFO, joystick_handler);
  return true;
}

// start the ADC FIFO and make sure it's put into a known good state
bool start_joystick(){
  adc_irq_set_enabled(true);
  adc_select_input(0); // in case it's something weird at startup
  irq_set_enabled(ADC_IRQ_FIFO, true);
  adc_run(true);
  return true;
}
