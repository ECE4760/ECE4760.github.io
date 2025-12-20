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
#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>
#include <stddef.h>
#include <../driver.h>

/*
  an interrupt-driven encoder driver, in which processing is handled by
  the MCU cores.

  a PIO-based approach could work, but may not be as tolerant to debouncing

  mask times are done by recording input times and discarding inputs within a
  certain time range. this reduces scheduler pressure.
*/

struct encoder_t {
  pin_no pin_a;
  pin_no pin_b;

  bool sw_swap_dir; // whether to swap pins a and b in software. corrects scroll
                    // direction
  uint8_t ticks_per_event; // how many encoder ticks to retain before an event
                           // is sent. used to slow scroll speed
  uint8_t scroll_modifier; // how much to multiply scroll amount by.

  uint32_t mask_time; // us: discard transitions in this range of time after an
                      // initial interrupt
  void(*handler);     // function pointer to handler
};

// slew rate:
// not sure if this will do anything but worth a try
#define TRY_SLEW_SLOW

// initialise all encoders with a configuration
bool init_encoders();

// start the ISRs for the configured encoders.
bool start_encoders();


#endif
