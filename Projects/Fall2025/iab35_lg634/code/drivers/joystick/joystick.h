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
#ifndef JOYSTICK_H
#define JOYSTICK_H

#include<stdint.h>
#include<stddef.h>
#include "../driver.h"


// bind the joystick button to a button_t
/*
  further processing of the readings is deferred.

  this driver will low-pass ADC readings, and compute measurements
  as a delta from a centre.
  
*/


struct joystick_t{
  // pins should be in [29:26]
  pin_no x_pin;
  pin_no y_pin;

  uint16_t poll_rate; // rate at which to run the adc, in Hz

  uint16_t centre_x; // should be the median value of 8 effective bits...
  uint16_t centre_y; // should be the median value of 8 effective bits...
  void (*handler);
};

bool init_joystick();

bool start_joystick();

#endif
