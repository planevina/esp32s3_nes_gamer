#pragma once
#include <Arduino.h>

typedef struct
{
    uint8_t cpu_usage;
    int8_t cpu_temp;
    uint16_t cpu_freq;
    uint16_t cpu_fan;

    uint8_t gpu_usage;
    int8_t gpu_temp;
    uint16_t gpu_freq;
    uint16_t gpu_fan;

    uint8_t ram_usage;
    uint16_t ram_avl;

    uint32_t net_upload_speed;
    uint32_t net_download_speed;

    uint8_t disk1_usage;
    int8_t disk1_temp;

    uint8_t disk2_usage;
    int8_t disk2_temp;

    int8_t mb_temp;
    
    uint32_t update_ts;
} aida64_pc_data;

extern aida64_pc_data pc_data;
extern IPAddress pc_host;

bool get_pc_data(void);
bool pc_mon_init();