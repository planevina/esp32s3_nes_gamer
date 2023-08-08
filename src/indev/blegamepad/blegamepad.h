#pragma once
#include <Arduino.h>

uint8_t wxpad_get_key_value();
void ble_gamepad_init();

extern TaskHandle_t TASK_BLE_HANDLE;

void task_ble(void *pvParameters);