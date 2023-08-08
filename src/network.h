#pragma once
#include <Arduino.h>

extern TaskHandle_t TASK_NETWORK_HANDLE;

void task_network(void *pvParameters);
bool get_wifi_status();
void clearWifiData();
