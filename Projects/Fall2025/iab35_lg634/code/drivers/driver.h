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
#ifndef DRIVER_H
#define DRIVER_H

// this header defines some types common to all drivers
#include <stdint.h>
#include <stddef.h>
#include <hardware/gpio.h>

constexpr uint32_t gpio_edges = GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE;

// an explicit, ergonomic type alias for pin numbers
typedef uint32_t pin_no;

// all events are packed into structs / unions. for fun, mostly

// event enumeration
enum event_sources: uint8_t{
  from_button = 0,
  from_sensor = 1,
  from_enc = 2,
  from_joystick = 3,
};

// i don't know how much the 'packed' attribute does here. 
union __attribute__((packed)) event_id{
  uint32_t raw; // alternate representation: raw value containing all of below.
  struct{
    uint8_t source; // which driver generated it
    uint8_t pin; // which pin it is associated with
    uint16_t misc; // up to the driver to define own functionality
  };
};

// buttons directly use data to expose a hold duration (on release events)
// button's misc value will be the event type
// joystick's misc value will be 0
// pmw3389's misc value will be the squal
struct driver_event_t{
  union event_id id; // identifier for this event
  uint32_t data; // data for this event
};

// the format in which a sensor / joystick should output its data
union sensor_data{
  uint32_t raw; // alternate representation: raw value containing all of below.

  // relative motion amounts represented as int16 (what the sensor returns)
  struct __attribute__((packed)) {
    int16_t delta_x;
    int16_t delta_y;
  };
};

union scroll_data{
  uint32_t raw; // alternate representation: raw value containing all of below.

  // relative motion amount represented as an int16 (semi-arbitrary decision)
  struct __attribute__((packed)){
    int16_t _unused; // extra padding in case the compiler does weird things
    int16_t amt;
  };
};

// the size of a driver_event_t, for allocation purposes
constexpr size_t event_size = sizeof(struct driver_event_t);


#endif
