#pragma once
#include <Arduino.h>
#include "main.h"

extern FILE_OBJ *romFiles;
extern uint8_t *cachedRom;
extern uint8_t romReadingBuffer[];
extern FILE_OBJ *mjpegFiles;    

void nes_loop();
void pc_monitor_loop();
void mjpeg_loop();
void setting_loop();
void menu_loop();