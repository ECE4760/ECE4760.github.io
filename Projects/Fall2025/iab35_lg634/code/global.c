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


#include "global.h"

// this file provides the actual hardware configuration exported in global.h

// hardware configuration for each button
const struct button_t button_configs[] = {
  {.bind = 13, .fall_trigger = true, .debounce_us = 1000, .handler = mouse_l},
  {.bind = 14, .fall_trigger = true, .debounce_us = 1000, .handler = mouse_m},
  {.bind = 15, .fall_trigger = true, .debounce_us = 1000, .handler = mouse_r},

  // user buttons
  {.bind = 16, .fall_trigger = true, .debounce_us = 1000, .handler = handle_chain_btn},
  {.bind = 17, .fall_trigger = true, .debounce_us = 1000, .handler = handle_chain_btn},
  {.bind = 18, .fall_trigger = true, .debounce_us = 1000, .handler = handle_chain_btn},
  {.bind = 19, .fall_trigger = true, .debounce_us = 1000, .handler = handle_chain_btn},
};

// number of buttons is auto-calculated
const size_t num_buttons = sizeof(button_configs) / sizeof(struct button_t);

// the below is sort of roundabout, but unfortunately num_buttons can't be constexpr and be passed around correctly
// the '32' number is because of the number of GPIO pins. in production you'd want to adjust this down.
static_assert(sizeof(button_configs) / sizeof(struct button_t) < 32, "bind: excess number of buttons");

// the encoder config.
// it's perhaps a bit overkill that there's the possibility of more than one, but hey.
const struct encoder_t encoder_configs[] = {
  [0] = {
  .pin_a = 9,
  .pin_b = 8,
  .sw_swap_dir = false,
  .ticks_per_event = 2,
  .mask_time = 3,
  .scroll_modifier = 1,
  .handler = mouse_wheel
  }
};

const size_t num_encoders = sizeof(encoder_configs) / sizeof(struct encoder_t);

// the below is sort of roundabout, but unfortunately num_buttons can't be constexpr and be passed around correctly
// 3 is sort of an arbitrary number, but is a good eough check.
static_assert(sizeof(encoder_configs) / sizeof(struct encoder_t) < 3, "bind: excess number of encoders");

// at this point i gave up and just had one configuration for these.
// both use less plentiful hardware resources anyway, so it's difficult to have more than 1
const struct pmw3389_cfg_t sensor_config = {
  .spi_chan = spi0,
  .csn = 1,
  .sclk = 2,
  .miso = 0,
  .mosi = 3,
  .reset_pin = 0,
  .motion_pin = 6,
  .handler = mouse_deltas
};

const struct joystick_t joystick_config = {
  .x_pin = 26,
  .y_pin = 27,
  .poll_rate = 1000,
  .centre_x = 256,
  .centre_y = 264,
  .handler = handle_chain_joy
};


// the execution context
equeue_t workqueue;

