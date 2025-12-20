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

#include "class/hid/hid.h"
#include "ebind.h"
#include "elib.h"
#include "types.h"

// this is where all binding happens

struct event_symbol_table table = {
    .curr_layer = 0,
    .scroll_mod = 1,
    .y_mod = 1,
    .x_mod = 1,
};

// layer defaults
st_data layer_defaults[3] = {
    [0] = default_layer, [1] = default_layer, [2] = default_layer};

// per button binds for 3, 4, 5, 6
// zeroes are gracefully handled for buttons
struct button_layer_t bind_3[3] = {[0] = {.option = 0, .chain = {incr_layer, nullptr}}};

struct button_layer_t bind_4[3] = {[0] = {.option = 0, .chain = {decr_layer, nullptr}}};
struct button_layer_t bind_5[3] = {
    [0] = {.option = {.id = opt_consumer,
                      .data = {.usage = HID_USAGE_CONSUMER_VOLUME_DECREMENT}},
           .chain = {send_from_bmap, nullptr}},
    [1] = {.option = {.id = opt_consumer,
                      .data = {.usage = HID_USAGE_CONSUMER_VOLUME_INCREMENT}},
           .chain = {send_from_bmap, nullptr}}
           

};
struct button_layer_t bind_6[3] = {
    [0] = {.option = {.id = opt_kb,
                      .data =
                          {
                              .mod = KEYBOARD_MODIFIER_LEFTCTRL,
                              .key = HID_KEY_A,
                          }},
           .chain = {send_from_bmap, nullptr}}};

// each layer can define a chain for the scroll / mouse
event_chain joystick_chains[3] = {
    [0] = {nullptr},
    [1] = {flatten_abs_x, nullptr},
};

event_chain encoder_chains[3];

// specify the top-level binding settings: how many configured layers
// there are for the joystick and encoder, and the list of button binds.
struct bind_opts opts = {
    .joychains = &joystick_chains[0],
    .num_joylayers = 2,
    .encchains = &encoder_chains[0],
    .num_enclayers = 1,

    .num_button_binds = 4,
    .button_binds = {{.btn = 16, .layers = 1, .config = &bind_3[0]},
                     {.btn = 17, .layers = 1, .config = &bind_4[0]},
                     {.btn = 18, .layers = 2, .config = &bind_5[0]},
                     {.btn = 19, .layers = 1, .config = &bind_6[0]}},

};
