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
#include "encoder.h"
#include "global.h"

#include <hardware/gpio.h>
#include <hardware/timer.h>
#include <stdckdint.h>


typedef enum enc_states: uint8_t{
 es0 = 0b00,
 es1 = 0b10,
 es2 = 0b11,
 es3 = 0b01, 
} enc_state_t;

static uint32_t trigger_times[3] = {0};
static enc_state_t states[3] = {0};
static int16_t ticks[3] = {0};

// a reduced transition table which only records
// valid transitions. forward in leftmost two bits, backward in others.
static const uint8_t ttable[4] = {
  [es0] = 0b10'01, // 00 -> 10 or 01
  [es1] = 0b11'00, // 10 -> 11 or 00
  [es2] = 0b01'10, // 11 -> 01 or 10
  [es3] = 0b00'11, // 01 -> 00 or 11
};

// try the reduced slew rate, in case it makes readings better
#define TRY_SLEW_SLOW

// helper to read the values off of the two pins
inline static enc_state_t enc_read(pin_no pin_a, pin_no pin_b){
  return (gpio_get(pin_a) << 1) | (gpio_get(pin_b));
}

// encoder interrupt handler
void encoder_handler(){
  uint32_t time = time_us_32();

  for (size_t i = 0; i < 1; i++){
    // retrieve the hardware config
    struct encoder_t curr = encoder_configs[i];

    // debouncing: disregard inputs which occur less than the mask time from the last interrupt
    // imperfect overflow andling.
    if (time < trigger_times[i]){
      continue;
    }
    if (time - trigger_times[i] < curr.mask_time){
      continue;
    }

    // acknowledge changes on the correct pin
    if (gpio_get_irq_event_mask(curr.pin_a) & gpio_edges){
      gpio_acknowledge_irq(curr.pin_a, gpio_edges);
    }
    else if (gpio_get_irq_event_mask(curr.pin_b) & gpio_edges){
      gpio_acknowledge_irq(curr.pin_b, gpio_edges);
    } else{
      continue;
    }

    // set trigger times
    trigger_times[i] = time;
 
    // get the new state
    enc_state_t new = enc_read(curr.pin_a, curr.pin_b);
    uint8_t valid = ttable[states[i]]; // get which next states are valid
    if (new == (valid & 0b11)){
      // backwards part valid
      ticks[i]--;
      states[i] = new;
    } else if (new == (valid >> 2)){
      // forward part valid
      ticks[i]++;
      states[i] = new;
    } else if (new == states[i]){
      states[i] = new; // nothing happened
    } else{
      states[i] = es0; // reset to zero
    }

    // if have had enough ticks to trigger an event,    
    if (ticks[i] == curr.ticks_per_event || ticks[i] == -curr.ticks_per_event){
      int16_t mod = (ticks[i] > 0) ? 1 : -1; // forward or backward
      struct driver_event_t * data = equeue_alloc(&workqueue,event_size);
      // i promise it's better than it looks...

      // reset tick count
      ticks[i] = 0;

      // multiply scroll modifier by forward or backward; populate remainder of info
      union scroll_data d;
      ckd_mul(&d.amt, mod, curr.scroll_modifier);
      
      data->data = d.raw;
      data->id.source = from_enc;
      data->id.pin = curr.pin_a;
      data->id.misc = i;

      // post event to schedule
      equeue_post(&workqueue,curr.handler,data); 
    }

  }
}

// set up the encoder hardware
bool init_encoders(){

  for (size_t i = 0; i < num_encoders; i++){
    // get the config
    struct encoder_t curr = encoder_configs[i];

    // configs can't have the two pins equal
    if (curr.pin_a == curr.pin_b){
      return false;
    }

    // does a swap in software if necessary
    if (curr.sw_swap_dir){
      pin_no tmp = curr.pin_a;
      curr.pin_a = curr.pin_b;
      curr.pin_b = tmp;  
    }

    // initialise the pins as pulled-up inputs.
    uint32_t pmask = (1 << curr.pin_a) + (1 << curr.pin_b);
    gpio_init_mask(pmask);
    gpio_pull_up(curr.pin_a);
    gpio_pull_up(curr.pin_b);


    // try slew rate if indicated.
    #ifdef TRY_SLEW_SLOW
      gpio_set_slew_rate(curr.pin_a, GPIO_SLEW_RATE_SLOW);
      gpio_set_slew_rate(curr.pin_b, GPIO_SLEW_RATE_SLOW);
    #endif

    // add the raw handler to the masked pins.
    gpio_add_raw_irq_handler_masked(pmask,encoder_handler);  
  }
  return true;
 
 
}

// start the encoder interrupt, initialise the FSMs
bool start_encoders(){
  for (size_t i = 0; i < num_encoders; i++){
    struct encoder_t curr = encoder_configs[i];
    // get an initial FSM reading
    states[i] = enc_read(curr.pin_a, curr.pin_b);

    // set IRQs to trigger on edges
    gpio_set_irq_enabled(curr.pin_a, gpio_edges, true);
    gpio_set_irq_enabled(curr.pin_b, gpio_edges, true);
  }
  // enable the IRQ for the bank in case it isn't valid.
  irq_set_enabled(IO_IRQ_BANK0,true);
  return true;
}
