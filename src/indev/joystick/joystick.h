#ifndef __JOYSTICK_H
#define __JOYSTICK_H
#include <Arduino.h>

typedef struct
{
    uint8_t pos;
    uint8_t value;
} USB_HID_JOYSTICK_DATA;

const USB_HID_JOYSTICK_DATA hid_key_map[12] =
    {
        {5, 0x2f}, // A
        {5, 0x4f}, // B
        {6, 0x10}, // SELECT
        {6, 0x20}, // START
        {1, 0x0},  // UP
        {1, 0xFF}, // DOWN
        {0, 0x0},  // LEFT
        {0, 0xFF}, // RIGHT
        {5, 0x1f}, // X
        {5, 0x8f}, // Y
        {6, 0x01}, // L
        {6, 0x02}  // R

};

uint8_t joystick_get_key_value();
void joystick_init();

#endif