#ifndef __USB_KEYBOARD_H
#define __USB_KEYBOARD_H
#include <Arduino.h>


extern uint8_t keyboard_get_key_value();
extern uint8_t keyboard_p2_get_key_value();
extern void keyboard_setup();

#endif