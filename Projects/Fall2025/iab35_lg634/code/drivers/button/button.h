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
#ifndef BUTTON_D
#define BUTTON_D

#include<stdint.h>
#include<stddef.h>
#include "../driver.h"


/*
  A generic means of handling buttons
  Also some utility functions
*/

// simple enum to represent type of event triggered
enum button_event: uint8_t{
  button_none,
  button_press,
  button_release
};


// the button hardware configuration structure
struct button_t{
  pin_no bind; // pin number of the button to bind to
  bool fall_trigger; // whether to trigger on falling edge (true)

  uint32_t debounce_us; // number of seconds to ignore output for debouncing

  void (*handler); // function pointer to what function to schedule
};


/*
  go through button_configs.
  configure the pins correctly.
  setup the unified IRQ for all button events
*/
bool init_button_pool();

/*
  start the IRQ for this button pool
*/
bool start_button_pool();


#endif
