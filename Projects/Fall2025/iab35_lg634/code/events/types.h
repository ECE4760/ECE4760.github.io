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

#ifndef EVENT_TYPES
#define EVENT_TYPES

#include <stdint.h>
#include "driver.h"

// config types

// which symbol to change in the symbol data
enum symbol_id: uint8_t{
  scroll_mod = 0,
  x_mod = 1,
  y_mod = 2,
};

// a way of addressing the whole symbol table at once
typedef union{
  uint32_t raw;
  struct{
    uint8_t _pad;
    uint8_t scroll_mod; // scroll modifier

    // x and y pan modifiers
    uint8_t x_mod; 
    uint8_t y_mod;
  };
} st_data;

// default layer information.
constexpr st_data default_layer = {
  ._pad = 0,
  .scroll_mod = 1,
  .x_mod = 1,
  .y_mod = 1,
};


struct event_symbol_table{
  uint8_t curr_layer; // maximum of three layers
  uint8_t scroll_mod; // scroll modifier

  // x and y pan modifiers
  uint8_t x_mod; 
  uint8_t y_mod;
  
};

// definition of an event chain
typedef void (*event)(void*);
typedef event event_chain[4];

// what data a button option refers to
enum button_opt_id: uint8_t{
  opt_data = 1,
  opt_kb,
  opt_mouse,
  opt_consumer,
};

// button binding information
typedef struct button_opt_t{
  enum button_opt_id id;
  union{
    // consumer control
    uint16_t usage;

    // symbol
    struct{
      int8_t num;
      enum symbol_id sym;
    };

    // mouse buttons
    struct{
      uint8_t mouse_buttons;
    };
   
    // keyboard
    struct{
      uint8_t mod;
      uint8_t key;
    };
  } data;
 
} button_opt;

// in each layer, other than its chain,
// a button is permitted one constant + one target field
// OR one HID binding (consumer, keyboard)
struct button_layer_t{
  button_opt option; // 24 bit
  event_chain chain;
};


// for a single pin, specifies its pin number, the number of layers, and its options for each layer.
struct button_bind{
  pin_no btn;
  uint8_t layers;
  struct button_layer_t * config;
};

// overall binding set.
// if num_{}layers is set to 0, the default handler is used.
// if num_{}layers is set to 1, one chain is used for all layers
// don't set it to 2, that would be sad :(
struct bind_opts{
  
  event_chain * joychains;
  uint8_t num_joylayers;

  event_chain *encchains;
  uint8_t num_enclayers;

  uint8_t num_button_binds;
  struct button_bind button_binds[];
  
};

#endif
