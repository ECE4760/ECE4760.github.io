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


#ifndef ELIB_H
#define ELIB_H

// a small 'standard library' of sorts for event handling
// GST = "global symbol table"
// all events fail silently if they do.


// convert a button press into a scroll event.
void transmute_scroll(void * in);

// convert a button press into a mouse x/y event
void transmute_mousex(void * in);
void transmute_mousey(void * in);

/*
  depending on the type of input, apply a scalar from GST
*/
void scale(void * in);

void shrink(void *in);

// convert a sensor type into a 1d scroll type.
void flatten_x(void * in);
void flatten_y(void * in);

// flatten by magnitude^2.
// use signedness on corresponding axis to determine output's sign
void flatten_abs_x(void * in);
void flatten_abs_y(void * in);

// for scroll, inverts the input
// for sensor, inverts both x and y
// for a button, inverts the event (pressed -> release, release -> pressed)
void invert(void * in);

// inverts the corresponding delta on a sensor
void invert_x(void * in);
void invert_y(void * in);

//! in the future, 'press based on thresh' would be cool

// targets

// given the button event and the implicit layer,
// set a variable in GST 
void set_from_bmap(void * in);

// given the button event and the implicit layer,
// send the corresponding keycode.
void send_from_bmap(void * in);

// changes to the layer
void incr_layer(void * in);
void decr_layer(void * in);
void reset_layer(void * in);

// change to last layer if button is held
void llayer_if_hold(void * in);



#endif
