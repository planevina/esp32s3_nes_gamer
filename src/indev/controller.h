#pragma once
#include <Arduino.h>

typedef enum
{
    GAMEPAD_KEY_A = 0x01,
    GAMEPAD_KEY_B = 0x02,
    GAMEPAD_KEY_SELECT = 0x04,
    GAMEPAD_KEY_START = 0x08,
    GAMEPAD_KEY_UP = 0x10,
    GAMEPAD_KEY_DOWN = 0x20,
    GAMEPAD_KEY_LEFT = 0x40,
    GAMEPAD_KEY_RIGHT = 0x80,
    GAMEPAD_KEY_X = 0x100,
    GAMEPAD_KEY_Y = 0x200,
} GAMEPAD_KEY_ENUM;

typedef struct
{
    union {
        struct {
            uint8_t JOY_A : 1;
            uint8_t JOY_B : 1;
            uint8_t JOY_SELECT : 1;
            uint8_t JOY_START : 1;
            uint8_t JOY_UP : 1;
            uint8_t JOY_DOWN : 1;
            uint8_t JOY_LEFT : 1;
            uint8_t JOY_RIGHT : 1;
        };
        uint8_t KEY_VALUE;
    };
} nes_pad_key_s;


extern nes_pad_key_s gamepad_p1;
extern nes_pad_key_s gamepad_p2;

void input_init();
void input_clear();
void input_refresh();