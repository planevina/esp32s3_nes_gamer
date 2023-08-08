#pragma once
#include <Arduino.h>

extern TaskHandle_t TASK_HOST_SHIELD_HANDLE;
extern uint8_t uhs_get_key_value();
extern void task_host_shield(void *pvParameters);
