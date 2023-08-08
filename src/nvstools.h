#pragma once
#include "nvs_flash.h"
#include "config.h"

void clear_devicename();
void save_devicename(char* name);
void load_devicename(char* ble_name);

bool clear_auto_play(uint8_t mode);
bool save_auto_play(char* file_name,uint8_t mode);
bool load_auto_play(char* file_name,uint8_t mode);

void set_auto_mode(uint8_t mode);

void load_config();
void save_config();
void clear_config();
