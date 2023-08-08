#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "config.h"

extern uint8_t *SCREENMEMORY[];
extern TaskHandle_t TASK_VID_HANDLE;

#ifdef USE_DOUBLE_BUFFER_DRAW_MJPEG
extern uint16_t *frame_odd_buf;
extern uint16_t *frame_even_buf;
#endif

extern SCRGFX *gfx;

void display_init();
void refresh_lcd(vid_notify_command_t cmd);
void videoTask(void *arg);