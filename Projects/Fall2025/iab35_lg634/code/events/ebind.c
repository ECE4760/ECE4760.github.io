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

#include "button.h"
#include "class/hid/hid.h"
#include "equeue.h"
#include "global.h"
#include "usb_descriptors.h"

#include <stdckdint.h>


// mouse reports are slightly queued in order to reduce amount of events sent
static hid_mouse_report_t queued_report = {
  0
};

// flags for whether events of a type have been queued
static bool mouse_queued = false;
static bool consumer_queued = false;
static bool kb_queued = false;

// wraps the tud task for equeue
void tud_wrap(void* e){
  tud_task();
}

// handle an arbitrary mouse button
void mouse_btn(void * e, uint8_t mask){
  // validate input
  if (e == nullptr){
    return;
  }
  struct driver_event_t * data = e;
  if (data->id.source != from_button){
    return;
  }

  // if a press, set the masked bit(s).
  // if a release, clear them
  switch (data->id.misc) {
    case button_press:
      queued_report.buttons |= mask;
      break;
    case button_release:
      queued_report.buttons &= ~mask;
      break;
    default:
      break;
  };

  // queue a mouse send if not already there.
  if (!mouse_queued){
     mouse_queued = true;
    equeue_call_in(&workqueue,1,mouse_end,nullptr);
  }
}

// make a button result in left click
void mouse_l(void * e){
  mouse_btn(e,MOUSE_BUTTON_LEFT);
}

// make a button result in right click.
void mouse_r(void * e){
  mouse_btn(e,MOUSE_BUTTON_RIGHT);
}

// make a button result in middle click.
void mouse_m(void * e){
  mouse_btn(e, MOUSE_BUTTON_MIDDLE);
}

// handle a set of mouse deltas.
void mouse_deltas(void * e){
  // input validation: only accept sensor and joystick data.
  if (e == nullptr){
    return;
  }
  struct driver_event_t * data = e;
  if (!(data->id.source == from_sensor || data->id.source == from_joystick)){
    return;
  }

  // add the x and y deltas to our accumulated existing report.
  union sensor_data d;
  d.raw = data->data;

  ckd_add(&queued_report.x, queued_report.x,-d.delta_x);
  ckd_add(&queued_report.y, queued_report.y,-d.delta_y);
  
  // queue a mouse send if not already existing.
  if (!mouse_queued){
    mouse_queued = true;
    equeue_call_in(&workqueue,1,mouse_end,nullptr);
  }

}

// handle a scroll delta.
void mouse_wheel(void * e){
  // input validation: only accept from encoder.
  if (e == nullptr){
    return;
  }
  struct driver_event_t * data = e;
  if (data->id.source != from_enc){
    return;
  }
  // add delta to the existing one.
  union scroll_data d;
  d.raw = data->data;
  ckd_add(&queued_report.wheel, queued_report.wheel, d.amt);

  // queue a mouse send if not already existing.
  if (!mouse_queued){
    mouse_queued = true;
    equeue_call_in(&workqueue,1,mouse_end,nullptr);
  }

}

// mouse send
void mouse_end(void * e){
  // instantly release the queued mouse event, as this event may fail
  mouse_queued = false;

  // wakeup bus if necessary
  if (tud_suspended()){
    tud_remote_wakeup();
    return;
  }
  // return if HID not ready, to avoid stalling
  if (!tud_hid_ready()){
    return;
  }
  // copy the report over to TinyUSB's memory.
  tud_hid_n_report(0, REPORT_ID_MOUSE, &queued_report, sizeof(hid_mouse_report_t));
}

// handle a button config which sends USB reports.
// e is in memory not managed by equeue
void btn_end(void * e, button_opt * data){
  // input validation: only handle non-none button events.
  if (e == nullptr){
    return;
  }
  struct driver_event_t * btn_data = e;
  if (btn_data->id.source != from_button){
    return;
  }

  if (btn_data->id.misc == button_none){
    return;
  }

  bool release = (btn_data->id.misc == button_release);

  // depending on the type of configured report,
  // send key presses only on a press; and use the mouse button handler
  // for mouse buttons.
  switch (data->id){
    case opt_consumer:
      if (!release){
        tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &data->data, 2);
        consumer_queued = true;
      }
      break;
    case opt_kb:
      if (!release){
        uint8_t keycode[6] = { 0 };
        keycode[0] = data->data.key;
        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, data->data.mod, keycode);
        kb_queued = true;
      }
      break;
    case opt_mouse:
      mouse_btn(e, data->data.mouse_buttons);
      break;
    default:
      break;
  }
}

// callback for a completed HID report
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
{
  static uint16_t empty = 0;
  switch (*report){
    case REPORT_ID_KEYBOARD:
      // if keyboard event sent, send a key release only once
      if (kb_queued){
        kb_queued = false; 
        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, nullptr);
      }
      break;
    case REPORT_ID_MOUSE:
      // clear the accumulator for the next report.
      queued_report.wheel = 0;
      queued_report.x = 0;
      queued_report.y = 0;
      // buttons may be held, so for glitch-free operation they aren't handled
      break;
    case REPORT_ID_CONSUMER_CONTROL:
      if (consumer_queued){
        // if consumer control event sent, send a key release only once.
        consumer_queued = false;
        tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &empty, 2);
      }
    default:
      break;
  }
}
