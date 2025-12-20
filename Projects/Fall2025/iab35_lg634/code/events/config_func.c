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

#include "ebind.h"

// handle a button event using the chain information
void handle_chain_btn(void * in){
  // reject no configuration
  if (opts.num_button_binds == 0){
    return;
  }

  // reject null data
  if (in == nullptr){
    return;
  }

  // reject non-button data
  struct driver_event_t *data = in;
  if (data->id.source != from_button) {
    return;
  }

  // scan for the right button in the bindings.
  for (uint8_t i = 0; i < opts.num_button_binds; i++){
    auto bind = opts.button_binds[i];
    if (bind.btn != data->id.pin){
      continue;
    }
    // clamp the layer to the configured max.
    uint8_t layer_to_use = table.curr_layer;
    if (layer_to_use > opts.button_binds[i].layers - 1) {
      layer_to_use = opts.button_binds[i].layers - 1;
    }
    
    // retrieve the config and call the chain
    auto cfg = bind.config[layer_to_use];
    for (uint8_t i = 0; i < 4; i++){
      if (cfg.chain[i] == nullptr){
        break;
      }
      cfg.chain[i](in);
    }
    
  }
}

// handle a joystick event using the chain information
void handle_chain_joy(void * in){
  // typical input validation
  if (opts.num_joylayers == 0){
    return;
  }

  if (in == nullptr){
    return;
  }

  struct driver_event_t *data = in;
  if (data->id.source != from_joystick) {
    return;
  }

  // clamp layer
  uint8_t layer_to_use = table.curr_layer;
  if (layer_to_use > opts.num_joylayers - 1) {
    layer_to_use = opts.num_joylayers - 1;
  }
  
  // retrieve and use chain
  auto chain = joystick_chains[layer_to_use];
  for (uint8_t i = 0; i < 4; i++) {
    if (chain[i] == nullptr) {
      break;
    }
    chain[i](in);
  }
  // convenience: the terminator is built-in
  mouse_deltas(in);
}

// handle an encoder event using the chain information
void handle_chain_enc(void * in){
  // typical input validation
  if (opts.num_joylayers == 0){
    return;
  }

  if (in == nullptr){
    return;
  }

  struct driver_event_t *data = in;
  if (data->id.source != from_enc) {
    return;
  }

  // clamp layer
  uint8_t layer_to_use = table.curr_layer;
  if (layer_to_use > opts.num_enclayers - 1) {
    layer_to_use = opts.num_enclayers - 1;
  }

  // retrieve and use chain
  auto chain = joystick_chains[layer_to_use];
  for (uint8_t i = 0; i < 4; i++) {
    if (chain[i] == nullptr) {
      break;
    }
    chain[i](in);
  }

  // convenience: the terminator is built-in.
  mouse_wheel(in);
  
}
