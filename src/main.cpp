//*******************************************************************************************
// “啊啊，老霸王其乐无穷啊”                                                                     
// ESP32-S3 老霸王学习机 V1.0                                                                 
// by 两个有相同情怀的中年男人 神秘藏宝室、萨纳兰的黄昏                                             
// 致我们终将（划掉）已经逝去的青春                                                               
// 模拟器代码重写自 NESCAT ESP32 NES模拟器v0.5     https://github.com/markoni985/NesCat             
// AIDA64后台代码改写自         https://github.com/ClimbSnail/HoloCubic_AIO/里的 pc_resource app       
// UI界面修改自                https://github.com/Melonhead60/_PixelPerfect                                 
// 适配老霸王ESP32—S3开发板，一块能当游戏机的开发板，一块不止能当游戏机的开发
// ”学编程，用老霸王“（手动狗头）    
//*******************************************************************************************

//参数定义修改请到config.h 

#include <Arduino.h>
#include <esp_task_wdt.h>
#include "main.h"
#include "display.h"
#include "ui.h"
#include "osd.h"
#include "network.h"
#include "sound.h"
#include "nvstools.h"
#include "emucore/emucore.h"
#include "indev/controller.h"
#include "fonts/fontPressStart2P.h"
#include "mjpeg/mjpegplayer.h"

#include <Arduino_GFX_Library.h>
#include <FFat.h>
#include <SD_MMC.h>

RUNNING_CFG cfg = {0};       // 运行中的配置
SAVE_NVS_CFG save_cfg = {0}; // NVS读出来或者要保存的配置

void ram_status()
{
    Serial.print("FREE SRAM: ");
    Serial.println(ESP.getFreeHeap());
    Serial.print("FREE PSRAM: ");
    Serial.println(ESP.getFreePsram());
}

void setup()
{
    delay(500);
    Serial.begin(115200);
    TaskHandle_t idle_0 = xTaskGetIdleTaskHandleForCPU(0);
    esp_task_wdt_delete(idle_0);

    uint32_t psram = ESP.getPsramSize();
    if (psram == 0)
    {
        SHOW_MSG_SERIAL("NO PSRAM\n")
        STAY_HERE
    }
    else if (psram <= 0x200000)
    {
        cfg.romMaxSize = 0x120000;
        SHOW_MSG_SERIAL("Max Nes romsize 1.2MB\n")
    }
    else
    {
        cfg.romMaxSize = 0x200000;
        SHOW_MSG_SERIAL("Max Nes romsize 2MB\n")
    }

    SCREENMEMORY = (uint8_t *)heap_caps_malloc(NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    
    // 分配ROM缓存
    cachedRom = (uint8_t *)heap_caps_malloc(cfg.romMaxSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == cachedRom)
    {
        SHOW_MSG_SERIAL("Allocating rom cache...[ FAIL ]\n")
        STAY_HERE
    }

    // 分配内存
    romFiles = (FILE_OBJ *)heap_caps_malloc(MAXFILES * sizeof(FILE_OBJ), MALLOC_CAP_SPIRAM);

    if (NULL == romFiles)
    {
        SHOW_MSG_SERIAL("Allocating rom filename cache...[ FAIL ]\n")
        STAY_HERE
    }

    // MJPEG缓存的文件名和NES的分开，原因嘛，1是因为PSRAM多，2是提高用户体验，切换后不用重复缓存
    mjpegFiles = (FILE_OBJ *)heap_caps_malloc(MAXFILES * sizeof(FILE_OBJ), MALLOC_CAP_SPIRAM);

    if (NULL == mjpegFiles)
    {
        SHOW_MSG_SERIAL("Allocating mjpeg filename cache...[ FAIL ]\n")
        STAY_HERE
    }

    // 分配mjpeg读取缓存
    mjpeg_buf = (uint8_t *)heap_caps_malloc(SCREEN_RES_HOR * SCREEN_RES_VER, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (NULL == mjpeg_buf)
    {
        SHOW_MSG_SERIAL("Allocating mjpeg buff...[ FAIL ]\n")
        STAY_HERE
    }

#ifdef USE_DOUBLE_BUFFER_DRAW_MJPEG
    // 双缓冲MJPEG播放，比直接刷屏提升约15%
    frame_odd_buf = (uint16_t *)heap_caps_malloc(SCREEN_RES_HOR * SCREEN_RES_VER * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    frame_even_buf = (uint16_t *)heap_caps_malloc(SCREEN_RES_HOR * SCREEN_RES_VER * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == frame_odd_buf || NULL == frame_even_buf)
    {
        SHOW_MSG_SERIAL("Allocating frame buff...[ FAIL ]\n")
        STAY_HERE
    }
#endif

    nvs_flash_init();

    load_config();

#if defined(HOLOCUBIC_PLUS_BOARD) || defined(GOD_VISION_BOARD)
    // 小电视和神之眼由于没有GPIO板，只能启用微信控制器
    cfg.controller = CONTROLLER_WECHAT_BLEPAD;
#elif defined(LCOS_PROJECTOR) || defined(LBW_DEV_BOARD_MINI)
    cfg.controller = CONTROLLER_WECHAT_BLEPAD;
    //cfg.controller = CONTROLLER_USB_HID_KBD;
#endif

    delay(200);

    // 初始化显示设备
    display_init();

    // 初始化存储
    SHOW_MSG_SERIAL("Initializing SD card...")

#if USE_1BIT_SDMMC
    SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN);
#else
    SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN, SD_MMC_D1_PIN, SD_MMC_D2_PIN, SD_MMC_D3_PIN);
#endif

    if (!SD_MMC.begin("/root", USE_1BIT_SDMMC, false, SDMMC_FREQ_DEFAULT))
    {
        SHOW_MSG_SERIAL("[ FAIL ]\n")
    }
    else
    {
        cfg.sdOK = 1;
        SHOW_MSG_SERIAL("[ OK ]\n")
    }
    // 创建刷屏任务
    xTaskCreatePinnedToCore(videoTask, "videoTask", 5120, NULL, 0, &TASK_VID_HANDLE, 0);

    if (cfg.controller != CONTROLLER_WECHAT_BLEPAD)
    {
        // 创建WIFI 任务
        xTaskCreatePinnedToCore(task_network, "networkTask", 4096, NULL, 1, &TASK_NETWORK_HANDLE, 1);
        // WIFI蓝牙同时开会爆内存，所以当启用微信蓝牙手柄时关闭WIFI任务
    }

#if SOUND_ENABLED
    i2s_init();
    if (!cfg.mute)
        xTaskCreatePinnedToCore(task_opening_sound, "opnSoundTask", 8192, NULL, 4, &TASK_OPENING_SOUND_HANDLE, 1);
#endif

    draw_lbw_text(); // 老霸王学习机

    // 为NES分配空间，就算不进入这个模式，也要先分配空间
    // 如果采用进入才动态分配的话，在模式之间多次切换会出错，所以提前分配好
    SHOW_MSG_SERIAL("Creating NES machine...")
    NESmachine = nes_create();
    if (NULL == NESmachine)
    {
        SHOW_MSG_SERIAL("[ FAIL ]\n")
        STAY_HERE
    }
    SHOW_MSG_SERIAL("[ OK ]\n")

    SHOW_MSG_SERIAL("Allocate rominfo...")
    if (NULL == NESmachine->rominfo)
        NESmachine->rominfo = (rominfo_t *)heap_caps_malloc(sizeof(rominfo_t), MALLOC_CAP_DMA);

    if (NULL == NESmachine->rominfo)
    {
        SHOW_MSG_SERIAL("[ FAIL ]\n")
        STAY_HERE
    }
    SHOW_MSG_SERIAL("[ OK ]\n")

    // 初始化输入设备
    input_init();
}

void loop()
{
    switch (cfg.currMode)
    {
    case MY_APP_NES:
        nes_loop();
        break;
    case MY_APP_AIDA64:
        pc_monitor_loop();
        break;
    case MY_APP_MJPEG:
        mjpeg_loop();
        break;
    case MY_APP_SETTING:
        setting_loop();
        break;
    default:
        menu_loop();
        break;
    }
    // ram_status();
    vTaskDelay(100);
}
