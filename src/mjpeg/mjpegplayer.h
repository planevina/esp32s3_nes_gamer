#pragma once
#include <Arduino.h>

extern uint8_t *mjpeg_buf;
bool init_mjpeg_file(char *filename);

void stopRead();
void mjpeg_draw_frame();