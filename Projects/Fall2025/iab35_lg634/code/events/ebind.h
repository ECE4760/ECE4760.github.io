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


#ifndef EVENT_H
#define EVENT_H

#include "types.h"
#include "tusb.h"


/*
  ebind contains the shared infrastructure for combining events and binding them to hardware
  
  this includes:
  - some common default events
  - implementations for tUSB functions
  - the binding config arrays

  to enable live remapping, one would need to map an input to the corresponding region of memory.
  to enable 'live' rechaining, one would need an enum with the desired functions encoded numerically, and a lookup table for their addresses
*/



extern st_data layer_defaults[3];
extern event_chain joystick_chains[3];
extern event_chain encoder_chains[3];
extern struct bind_opts opts;

extern struct event_symbol_table table;

// use these handlers to make a device read from its configured chain
void handle_chain_btn(void *);
void handle_chain_joy(void *);
void handle_chain_enc(void *);


void mouse_btn(void * e, uint8_t mask);

// events for basic mouse functionality
// if used in lieu of chain, becomes 'simple' mouse

// expects button event data
// based on internal data, handle a press / release
void mouse_l(void *);
void mouse_r(void *);
void mouse_m(void*);

// expects a sensor_data field
// will validate that source is sensor / joystick
void mouse_deltas(void *);

// expects a scroll_data field
// will validate that source is encoder
void mouse_wheel(void *);

// send all pending mouse events
void mouse_end(void *);

// processes a button_opt plus button event
// excludes variable setting
void btn_end(void *, button_opt * data);



// tinyusb
void tud_wrap(void* );
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len);

#endif
