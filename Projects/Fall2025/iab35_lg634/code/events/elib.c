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

#include "elib.h"

#include "button.h"
#include "ebind.h"
#include "../drivers/driver.h"
#include<stdckdint.h>

// convert a button press into a scroll amount.
void transmute_scroll(void * in){
  if (in == nullptr){
    return;
  }
  struct driver_event_t * data = in;
  if (data->id.source != from_button){
    return;
  }

  data->id.source = from_enc;
  union scroll_data d;
  d.amt = 1;
  data->data = d.raw;

}

// convert a button press into a mouse x/y event
void transmute_mousex(void * in){
  if (in == nullptr){
    return;
  }
  struct driver_event_t * data = in;
  if (data->id.source != from_button){
    return;
  }

  data->id.source = from_sensor;
  union sensor_data d;
  d.delta_x = 1;
  data->data = d.raw;
  
}

void transmute_mousey(void * in){
  if (in == nullptr){
    return;
  }
  struct driver_event_t * data = in;
  if (data->id.source != from_button){
    return;
  }

  data->id.source = from_sensor;
  union sensor_data d;
  d.delta_y = 1;
  data->data = d.raw;
}

/*
  depending on the type of input, apply a scalar from GST
*/
void scale(void * in){
  if (in == nullptr){
    return;
  }
  struct driver_event_t * data = in;
  switch (data->id.source){
    case from_sensor:
    case from_joystick:
      // joystick /sensor modifier
      union sensor_data in_d;
      in_d.raw = data->data;
      ckd_mul(&in_d.delta_x,in_d.delta_x,table.x_mod);
      ckd_mul(&in_d.delta_y,in_d.delta_y,table.y_mod);
      data->data = in_d.raw;
      break;
    case from_enc:
      // scroll modifier
      union scroll_data in_sc;
      in_sc.raw = data->data;
      ckd_mul(&in_sc.amt , in_sc.amt,table.scroll_mod);
      data->data = in_sc.raw;
      break;
    default:
      break;
  };
}

// like the above, but dividing rather than multiplying.
void shrink(void * in){
  if (in == nullptr){
    return;
  }
  struct driver_event_t * data = in;
  switch (data->id.source){
    case from_sensor:
    case from_joystick:
      union sensor_data in_d;
      in_d.raw = data->data;

      // make sure not to divide by zero!
      if (table.x_mod != 0){
        in_d.delta_x = (int16_t)(in_d.delta_x / table.x_mod);
      }
      if (table.y_mod != 0){
        in_d.delta_y = (int16_t)(in_d.delta_y/ table.y_mod);
        
      }
      
      data->data = in_d.raw;
      break;
    case from_enc:
      union scroll_data in_sc;
      in_sc.raw = data->data;
      if (table.scroll_mod != 0){
        in_sc.amt = (int16_t) (in_sc.amt / table.scroll_mod);
      }
      data->data = in_sc.raw;
      break;
    default:
      break;
  };

  
}

// convert a sensor type into a 1d scroll type by taking only its delta x/y.
void flatten_x(void * in){
  if (in == nullptr){
    return;
  }
  struct driver_event_t * data = in;
  if (data->id.source != from_sensor ){
    return;
  }

  data->id.source = from_enc;
  union scroll_data d;
  union sensor_data in_d;
  in_d.raw = data->data;
  d.amt = in_d.delta_x;

  data->data = d.raw;

}
void flatten_y(void * in){
  if (in == nullptr){
    return;
  }
  struct driver_event_t * data = in;
  if (data->id.source != from_sensor ){
    return;
  }

  data->id.source = from_enc;
  union scroll_data d;
  union sensor_data in_d;
  in_d.raw = data->data;
  d.amt = in_d.delta_y;

  data->data = d.raw;
  
}

// flatten an x-y value to its magnitude^2 in scroll amount.
// use signedness on corresponding axis to determine output's sign
void flatten_abs_x(void * in){
  if (in == nullptr){
    return;
  }
  struct driver_event_t * data = in;
  if (data->id.source != from_sensor ){
    return;
  }

  data->id.source = from_enc;
  union scroll_data d;
  union sensor_data in_d;
  in_d.raw = data->data;

  int16_t sum, a, b;
  ckd_mul(&a, in_d.delta_x, in_d.delta_x);
  ckd_mul(&b, in_d.delta_y, in_d.delta_y);
  ckd_add(&sum, a,b);

  if (in_d.delta_x < 0){
    sum *= -1;
  }
  
  d.amt = sum;
  data->data = d.raw;
}

void flatten_abs_y(void * in){
  if (in == nullptr){
    return;
  }
  struct driver_event_t * data = in;
  if (data->id.source != from_sensor ){
    return;
  }

  data->id.source = from_enc;
  union scroll_data d;
  union sensor_data in_d;
  in_d.raw = data->data;

  int16_t sum, a, b;
  ckd_mul(&a, in_d.delta_x, in_d.delta_x);
  ckd_mul(&b, in_d.delta_y, in_d.delta_y);
  ckd_add(&sum, a,b);

  if (in_d.delta_y < 0){
    sum *= -1;
  }
  
  d.amt = sum;
  data->data = d.raw;
  
}

// for scroll, inverts the input
// for sensor, inverts both x and y
// for a button, inverts the event (pressed -> release, release -> pressed)
void invert(void * in){
  if (in == nullptr){
    return;
  }
  struct driver_event_t * data = in;

}

// inverts the corresponding delta on a sensor
void invert_x(void * in){
  if (in == nullptr){
    return;
  }
  struct driver_event_t * data = in;
  if (data->id.source != from_sensor ){
    return;
  }
  union sensor_data in_d;
  in_d.raw = data->data;
  in_d.delta_x *= -1;

  data->data = in_d.raw;
}

void invert_y(void * in){
  if (in == nullptr){
    return;
  }
  struct driver_event_t * data = in;
  if (data->id.source != from_sensor ){
    return;
  }
  union sensor_data in_d;
  in_d.raw = data->data;
  in_d.delta_y *= -1;

  data->data = in_d.raw;
  
}

//! in the future, 'press based on thresh' would be cool

// targets

// given the button event and the implicit layer,
// set a variable in GST 
void set_from_bmap(void * in){
  if (in == nullptr){
    return;
  }
  struct driver_event_t * data = in;
  if (data->id.source != from_button){
    return;
  }

  button_opt curr_config;
  for (uint8_t i = 0; i < opts.num_button_binds; i++){
    if (opts.button_binds[i].btn == data->id.pin){
      // clamp the layer
      uint8_t layer_to_use = table.curr_layer;
      if (layer_to_use > opts.button_binds[i].layers - 1){
        layer_to_use = opts.button_binds[i].layers;
      }

      // based on what information in the current layer the button is configured to set, do so.
      auto curr_layer = opts.button_binds[i].config[layer_to_use];
      if (curr_layer.option.id == opt_data){
        switch (curr_layer.option.data.sym){
          case scroll_mod:
            table.scroll_mod = curr_layer.option.data.num;
            break;
          case x_mod:
            table.x_mod = curr_layer.option.data.num;
            break;
          case y_mod:
            table.y_mod = curr_layer.option.data.num;
            break;
          default:
            break;
        }
      }
    }
  }
}

// given the button event and the implicit layer,
// send the corresponding keycode.
void send_from_bmap(void * in){
  if (in == nullptr) {
    return;
  }
  struct driver_event_t *data = in;
  if (data->id.source != from_button) {
    return;
  }

  button_opt curr_config;
  for (uint8_t i = 0; i < opts.num_button_binds; i++) {
    if (opts.button_binds[i].btn == data->id.pin) {
      uint8_t layer_to_use = table.curr_layer;
      // clamp layer
      if (layer_to_use > opts.button_binds[i].layers - 1) {
        layer_to_use = opts.button_binds[i].layers - 1;
      }

      // if not setting data (requires function above), call btn_end
      // to handle finding and sending the keypress.
      auto curr_layer = opts.button_binds[i].config[layer_to_use];
      if (curr_layer.option.id != opt_data) {
        btn_end(in, &curr_layer.option);
      }
    }
  }
}

// increment / decrement layers, set the defaults for these layers.
void incr_layer(void * in){
  if (table.curr_layer < 2){
    table.curr_layer++;
    table.scroll_mod = layer_defaults[table.curr_layer].scroll_mod;
    table.x_mod = layer_defaults[table.curr_layer].x_mod;
    table.y_mod = layer_defaults[table.curr_layer].x_mod;
  }
}
void decr_layer(void * in){
  if (table.curr_layer > 0){
    table.curr_layer--;
    table.scroll_mod = layer_defaults[table.curr_layer].scroll_mod;
    table.x_mod = layer_defaults[table.curr_layer].x_mod;
    table.y_mod = layer_defaults[table.curr_layer].x_mod;
    
  }
}

// reset layer to 0 and adjust defaults accordingly.
void reset_layer(void * in){
  table.curr_layer = 0;
  table.scroll_mod = layer_defaults[0].scroll_mod;
  table.x_mod = layer_defaults[0].x_mod;
  table.y_mod = layer_defaults[0].x_mod;

}

// if a button is held (i.e. on its press transition),
// change the layer to the last one.
// on release, return it to the previously held layer.
void llayer_if_hold(void * in){
  if (in == nullptr) {
    return;
  }
  struct driver_event_t *data = in;
  if (data->id.source != from_button) {
    return;
  }
  static uint8_t prev_layer; // keep track of the previous layer number
  if (data->id.raw == button_press){
    prev_layer = table.curr_layer;
    table.curr_layer = 2;
    table.scroll_mod = layer_defaults[2].scroll_mod;
    table.x_mod = layer_defaults[2].x_mod;
    table.y_mod = layer_defaults[2].x_mod;
  } else{
    table.curr_layer = prev_layer;
    table.scroll_mod = layer_defaults[prev_layer].scroll_mod;
    table.x_mod = layer_defaults[prev_layer].x_mod;
    table.y_mod = layer_defaults[prev_layer].x_mod;

  }
  
}
