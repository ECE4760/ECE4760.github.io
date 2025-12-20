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
#include "button.h"
#include "driver.h"
#include <hardware/gpio.h>
#include <hardware/regs/intctrl.h>
#include <hardware/timer.h>
#include <string.h>
#include <stdckdint.h>

#include "equeue.h"
#include "global.h"

// trigger times for debouncing
volatile static uint32_t trigger_times[32] = {0};
// trigger state of each button
volatile static uint32_t trig = 0;


/*
  helper to handle a single button.

  accepts: pointer to button struct, the event, and the current time
  event should be set to true if falling edge triggered; else rising
*/
enum button_event handle_button(uint8_t idx, bool event, uint32_t time){
  // get the associated hardware config
  struct button_t btn = button_configs[idx];

  /*
    in case the time is less than the recorded trigger time, reject
    and reset the trigger time. not sure if it behaves properly.
  */
  if (time < trigger_times[idx]){
    trigger_times[idx] = time;
    return button_none;
  }

  // calculate the time difference since last trigger.
  uint32_t diff;
  bool over = ckd_sub(&diff, time, trigger_times[idx]);

  // if overflowed for some reason, reset the trigger time
  if (over){
    trigger_times[idx] = time;
    return button_none;
  }

  // if still in debounce period, reject
  if (diff < btn.debounce_us){
    return button_none;
  }

  // otherwise, update trigger time
  trigger_times[idx] = time;
  
  // if trigger, indicate press in the trig mask
  bool bmask = trig & (1 << idx);

  // bmask is used to ensure that every press has a corresponding release
  if ((event == btn.fall_trigger) && !bmask){
    trig |= (1 << idx);
    return button_press;
  }

  // if, for some reason, a retrigger occurs without the button being released,
  // send a release instead
  trig &= ~(1 << idx);

  return button_release;

}

// ISR for button GPIO pins
void button_handler() {
  // record the time at trigger
  // some time may elapse during handling, we assume it's negligible
  uint32_t time = time_us_32();
  
  // for every button
  for (size_t i = 0; i < num_buttons; i++) {
    // get its config and event mask
    struct button_t curr = button_configs[i];
    uint32_t mask = gpio_get_irq_event_mask(curr.bind);

    // acknowledge rise / fall events if they exist. else, move on
    bool fall = false; // whether the event was a fall
    if (mask & GPIO_IRQ_EDGE_FALL) {
      gpio_acknowledge_irq(curr.bind, GPIO_IRQ_EDGE_FALL);
      fall = true;
    } else if (mask & GPIO_IRQ_EDGE_RISE) {
      gpio_acknowledge_irq(curr.bind, GPIO_IRQ_EDGE_RISE);
    } else {
      continue;
    }

    // call the button handler convenience function to decide what to do.
    enum button_event res = handle_button(i, fall, time);

    // if 'what to do' isn't nothing,
    if (res != button_none) {
      // allocate a new driver event
      struct driver_event_t *data = equeue_alloc(&workqueue, event_size);

      // set all the data fields accordingly
      data->data = (res == button_release) ? (time - trigger_times[i]): 0; //the hold duration
      data->id.source = from_button;
      data->id.pin = curr.bind;
      data->id.misc = 0;
      data->id.misc = res;

      // post the handler function with a pointer to the allocated event data.
      equeue_post(&workqueue, curr.handler, data);
    }
  }
}

// set up the hardware for the button pool
// unfortunately the SDK functions don't really indicate failure
// so for now it returns true.
bool init_button_pool() {
  uint32_t irq_mask = 0; // pins to turn the IRQ on for

  // for every button,
  for (size_t i = 0; i < num_buttons; i++) {
    // get its config
    struct button_t curr = button_configs[i];
    // update the mask
    irq_mask |= (1 << curr.bind);
    // init and pull up the pin
    gpio_init(curr.bind);
    gpio_pull_up(curr.bind);
  }
  // add a raw irq for the masked pins
  gpio_add_raw_irq_handler_masked(irq_mask, button_handler);
  return true;
}

// start the interrupts for the button pool.
// unfortunately the SDK functions don't really indicate failure
// so for now it returns true.

bool start_button_pool() {
  // for every button,
  for (size_t i = 0; i < num_buttons; i++) {
    // get its config and enable the IRQ on the associated pin.
    struct button_t curr = button_configs[i];
    gpio_set_irq_enabled(curr.bind, gpio_edges, true);
  }
  // very cool. this is necessary
  irq_set_enabled(IO_IRQ_BANK0, true);
  return true;
}
