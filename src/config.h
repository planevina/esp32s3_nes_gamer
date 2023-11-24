#ifndef __CONFIG_H
#define __CONFIG_H
#include <stdint.h>


//五选一
//#define LBW_GAME_DEV_BOARD
//#define HOLOCUBIC_PLUS_BOARD
//#define GOD_VISION_BOARD
//#define LBW_DEV_BOARD_MINI
#define LCOS_PROJECTOR

//注意，UI只适配了320x240 和 240x240 ，其他分辨率可能会导致UI显示不全

// 老霸王开发板===================================
#ifdef LBW_GAME_DEV_BOARD
#define SCREEN_RES_HOR 320
#define SCREEN_RES_VER 240

#define SCRGFX Arduino_ST7789

#define TFT_BLK_ON_LOW  // 低电平打开背光
#define TFT_IS_IPS true // IPS屏幕
#define TFT_ROTATION 3  // 0排线宽边向上  1顺时针旋转90 ，3 顺时针旋转270

#define TFT_BL 4
#define TFT_DC 9
#define TFT_CS 10
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_MISO 13
#define TFT_RST 14

#define BAT_ADC_PIN 5
#define TOUCH_CS 15 // 暂未使用

// 集成手柄
#define ADC_X 1
#define ADC_Y 2
#define PIN_A 42
#define PIN_B 41
#define PIN_SELECT 39
#define PIN_START 40
#define PIN_EXTRA 38 // 手柄预留的gpio

// USB HOST SHIELD
// 由于库文件无法包含这个config文件，故以下定义只是证明该IO被使用
// 实际使用需要在platform.ini的build_flag 里面增加  -DBOARD_LBW_DEV
#define UHS_SPI_SCK 48
#define UHS_SPI_MISO 6
#define UHS_SPI_MOSI 7
#define UHS_SPI_SS 3
#define UHS_INT 45
// #define UHS_RST 46 //暂时不接这个，直接接3.3
///////////////////////////////////////////////
// UHS接线图示，芯片面对人，USBA口在左边
// 模组正上方的跳线顺序
// 模组  5v  3v3  INT  GPX  MOSI  MISO  SS   SCLK   RST   GND
//           |    |          |     |    |     |     |     |
// 老霸王 NA  3v3  45    NA    7     6    3    48    3v3   GND
// 老霸王A口母座的D-，D+ 焊盘连接到模组的D- D+，模组靠5v的那个孔为D-
///////////////////////////////////////////////

// SDMMC
#define USE_1BIT_SDMMC true
#define SD_MMC_D0_PIN 47
#define SD_MMC_CLK_PIN 21
#define SD_MMC_CMD_PIN 8

#define SOUND_ENABLED true


// AUDIO_i2S
#define I2S_BCK_IO 17 // BCK
#define I2S_WS_IO 16  // LCK
#define I2S_DO_IO 18  // DIN
#define I2S_DI_IO (-1)

// 输入设备
// 输入设备只需要定义GPIO 和 UHS，其他的储存在NVS里面按需加载
#define GPIO_PAD_ENABLED
//#define USB_HOST_SHIELD_ENABLED


#ifdef USB_HOST_SHIELD_ENABLED
// USB HOST SHIELD 的PIN定义

// xboxold.cpp BLACK WHITE

// UsbCore.h 37行
// avrpins.h 1711行
// usbhost.h 113行，53行

// #define USB_PS4_JOY_ENABLED
#define USB_XBOX_ONE_JOY_ENABLED
// #define USB_KEYBOARD_ENABLED
// #define BT_PS4_JOY_ENABLED
// #define USB_HID_JOY_ENABLED
// #define WIRELESS_XBOX_ONE_JOY_ENABLED
#endif

#endif
// 老霸王开发板结束===================================



#ifdef LBW_DEV_BOARD_MINI
#define SCREEN_RES_HOR 320
#define SCREEN_RES_VER 240

#define SCRGFX Arduino_ST7789

#define TFT_BLK_ON_LOW  // 低电平打开背光
#define TFT_IS_IPS true // IPS屏幕
#define TFT_ROTATION 3  // 0排线宽边向上  1顺时针旋转90 ，3 顺时针旋转270

#define TFT_BL 2
#define TFT_DC 4
#define TFT_CS 41
#define TFT_MOSI 5
#define TFT_SCLK 42
#define TFT_MISO 13
#define TFT_RST 1

// SDMMC
#define USE_1BIT_SDMMC false
#define SD_MMC_D0_PIN 46
#define SD_MMC_D1_PIN 3
#define SD_MMC_D2_PIN 12
#define SD_MMC_D3_PIN 11
#define SD_MMC_CLK_PIN 9
#define SD_MMC_CMD_PIN 10

#define SOUND_ENABLED true

// AUDIO_i2S
#define I2S_BCK_IO 39 // BCK
#define I2S_WS_IO 40  // LCK
#define I2S_DO_IO 38  // DIN
#define I2S_DI_IO (-1)

// 输入设备
#endif


#ifdef HOLOCUBIC_PLUS_BOARD
#define SCREEN_RES_HOR 240
#define SCREEN_RES_VER 240

#define SCRGFX Arduino_ST7789
// 低电平打开背光
#define TFT_BLK_ON_LOW
#define TFT_IS_IPS false
#define TFT_ROTATION 0 // 0排线宽边向上  1顺时针旋转90 ，3 顺时针旋转270

#define TFT_MISO 13
#define TFT_SCLK 12
#define TFT_MOSI 11
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 14
#define TFT_BL 4

#define USE_1BIT_SDMMC true
#define SD_MMC_D0_PIN 40
#define SD_MMC_CLK_PIN 2
#define SD_MMC_CMD_PIN 1

#define SOUND_ENABLED false

#endif

#ifdef GOD_VISION_BOARD
#define SCREEN_RES_HOR 240
#define SCREEN_RES_VER 240

#define SCRGFX Arduino_GC9A01

#define TFT_IS_IPS true
#define TFT_ROTATION 0

#define TFT_BL 5
#define TFT_DC 21
#define TFT_CS 14
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_MISO -1
#define TFT_RST -1

// SDMMC
#define USE_1BIT_SDMMC false
#define SD_MMC_D0_PIN 39
#define SD_MMC_D1_PIN 40
#define SD_MMC_D2_PIN 2
#define SD_MMC_D3_PIN 42
#define SD_MMC_CLK_PIN 41
#define SD_MMC_CMD_PIN 38

// 老款S3神之眼
#if 0
#define TFT_BL 14
#define TFT_DC 10
#define TFT_CS 11
#define TFT_MOSI 13
#define TFT_SCLK 12
#define TFT_MISO -1
#define TFT_RST 21

// SDMMC
#define USE_1BIT_SDMMC true
#define SD_MMC_D0_PIN 40
#define SD_MMC_CLK_PIN 41
#define SD_MMC_CMD_PIN 42
#endif

#define SOUND_ENABLED false

#endif

#ifdef LCOS_PROJECTOR
#include "hw_lcos_projector.h"
#endif



 //USB HOST SHILED和原生CDC口共享A口，同时只能启用一个
#ifdef USB_HOST_SHIELD_ENABLED
#define USB_A_PORT_MODE_UHS 
#else
#define USB_A_PORT_MODE_CDC
#endif

#define SHOW_MSG_BOTH(x) Serial.print(x);gfx->print(x);
#define SHOW_MSG_SERIAL(x) Serial.print(x);
#define STAY_HERE while (1){};

#define UI_TEXT_COLOR 0xff9e
#define UI_TEXT_SELECTED_COLOR 0xffff
#define UI_STATUSBAR_CURSOR_X 10
#define UI_STATUSBAR_CURSOR_Y 228

#if SCREEN_RES_HOR == 320
#define UI_GAME_LIST_COLOR 0x0
#define UI_GAME_LIST_SELECTED_COLOR 0xffff
#elif SCREEN_RES_HOR == 240
#define UI_GAME_LIST_COLOR 0xc618
#define UI_GAME_LIST_SELECTED_COLOR 0xffff
#else
#define UI_GAME_LIST_COLOR 0x0
#define UI_GAME_LIST_SELECTED_COLOR 0xffff
#endif

#if SCREEN_RES_HOR == 320
#define UI_MJPEG_LIST_COLOR 0x0
#define UI_MJPEG_LIST_SELECTED_COLOR 0xffff
#elif SCREEN_RES_HOR == 240
#define UI_MJPEG_LIST_COLOR 0xc618
#define UI_MJPEG_LIST_SELECTED_COLOR 0xffff
#else
#define UI_MJPEG_LIST_COLOR 0x0
#define UI_MJPEG_LIST_SELECTED_COLOR 0xffff
#endif

#define UI_CONTROLLER_LIST_COLOR 0xff9e
#define UI_CONTROLLER_LIST_ACTIVED_COLOR 0x247D

#define UI_SETTING_ACTIVED_COLOR 0x247D
#define UI_SETTING_COLOR 0x73CE

#define UI_MAIN_MENU_TEXT_COLOR 0xff9e
#define UI_MAIN_MENU_TEXT_FAV_COLOR RGB565_ORANGE

#define UI_CONTROL_DEBOUNCE_MS1  200
#define UI_CONTROL_DEBOUNCE_MS2  400

#define UI_AIDA64_COLOR 0xffff
#define PC_MONITOR_UPDATE_INTERVAL 2000
#define DEBUG_SHOW_MJPEG_FPS // 在串口显示MJPEG的帧率
#define USE_DOUBLE_BUFFER_DRAW_MJPEG

#define AP_SSID "LBWGAMER-Ap"
#define AP_PWD "lbwgamer"

#define DEVICE_BLE_NAME "LBW_GAMER"
#define NVS_STOR_NAME "LBWCFG"

#define MAXFILES 255                 // 最大文件数
#define MAXFILENAME_LENGTH 62        // 文件名最长 62+2=64 ，最好是4的倍数
#define FILES_PER_PAGE 10            // 每页文件数
#define ROM_READING_BUFFER_SIZE 4096 // Rom读取缓存

// NES模拟器相关定义=============================================
#define NES_FOLDER ("/NES/")

#define HOST_LITTLE_ENDIAN
#define ZERO_LENGTH 0

#define UNUSED_NES(x) ((x) = (x))

#define NTSC
#ifdef PAL
#define NES_REFRESH_RATE 50
#else
#define NES_REFRESH_RATE 60
#endif

#define NES_VISIBLE_HEIGHT 240
#define NES_SCREEN_WIDTH 256
#define NES_SCREEN_HEIGHT 240

// AUDIO_SETUP
#define DEFAULT_SAMPLERATE 24000
#define DEFAULT_FRAGSIZE 200
// #define DEFAULT_FRAGSIZE (DEFAULT_SAMPLERATE / NES_REFRESH_RATE)

#define MAX_MEM_HANDLERS 32

#define NES_CLOCK_DIVIDER 12 // 默认 12，如果运行速度太快旧改低，数值越低越慢
// #define  NES_MASTER_CLOCK     21477272.727272727272
#define NES_MASTER_CLOCK (236250000 / 11)
#define NES_SCANLINE_CYCLES (1364.0 / NES_CLOCK_DIVIDER)
#define NES_FIQ_PERIOD (NES_MASTER_CLOCK / NES_CLOCK_DIVIDER / 60)

#define NES_RAMSIZE 0x800

#define NES_SKIP_LIMIT (NES_REFRESH_RATE / 5)

#define NES6502_NUMBANKS 16
#define NES6502_BANKSHIFT 12
#define NES6502_BANKSIZE (0x10000 / NES6502_NUMBANKS)
#define NES6502_BANKMASK (NES6502_BANKSIZE - 1)
// NES模拟器相关定义结束=============================================

typedef enum
{
    MY_APP_NONE = 0,  // 选择画面
    MY_APP_NES,       // NES游戏机
    MY_APP_AIDA64,    // AIDA64
    MY_APP_MJPEG,     // MJPEG播放
    MY_APP_SETTING    // 设置
} app_enmu_t;

#define APP_MENU_NUM 3   // 主菜单个数

typedef struct
{
    app_enmu_t APP_ID;         // 菜单对应APP ID
    const char *menu_text;     // 菜单文本
    const uint16_t *menu_icon; // 菜单图标
    uint8_t icon_height;       // 图标高度
    int16_t text_x;            // 文本x位置
} APP_MENU_OBJ;

typedef enum
{
    VID_NONE = 0x0000,
    VID_DRAW_NES_FRAME = 0x0001,
#ifdef USE_DOUBLE_BUFFER_DRAW_MJPEG
    VID_DRAW_MJPEG_ODD_FRAME = 0x0002,
    VID_DRAW_MJPEG_EVEN_FRAME = 0x0004,
#endif
} vid_notify_command_t;


typedef enum
{
    CONTROLLER_NONE = 0,
    CONTROLLER_USB_HID_KBD,
    CONTROLLER_USB_HID_JOYSTICK,
    CONTROLLER_WECHAT_BLEPAD,
    CONTROLLER_USB_HOST_SHIELD
} controller_enum;

typedef struct
{
    uint16_t file_size; // KB or MB
    char file_name[MAXFILENAME_LENGTH];
} FILE_OBJ;

typedef struct
{
    uint8_t currMode;                // 当前运行
    uint8_t nesEnterFlag : 1;        // 是否已经进入过nes模式
    uint8_t pcmonEnterFlag : 1;      // 是否已经进入过pcmon模式
    uint8_t mjpegEnterFlag : 1;      // 是否已经进入过mjpeg模式
    uint8_t controller : 5;          // 手柄
    uint8_t sdOK : 1;                // 是否已初始化sd卡
    uint8_t romFilenameCached : 1;   // 是否已缓存rom文件名
    uint8_t mjpegFilenameCached : 1; // 是否已缓存mjpeg文件名
    uint8_t nesPower : 1;            // nes开关
    uint8_t mute : 1;                // 静音
    uint8_t mjpegPlayMode : 2;       // mjpeg播放模式（暂时没用）
    uint8_t : 1;                     // 占位
    uint8_t loadedRomFileNames;      // ROM文件名列表中的文件个数
    uint8_t loadedMjpegFileNames;    // MJPEG文件名列表中的文件个数
    uint8_t menuCursor;              // 菜单索引
    uint8_t menuPage;                // 当前菜单页
    uint32_t romMaxSize;             // 可加载的最大rom大小
} RUNNING_CFG;

typedef struct
{
    uint8_t favMode;               // 开机后进入的模式
    uint8_t wifiMode : 2;          // 0ap 1sta
    uint8_t usbMscMode : 1;        // 0正常，1msc，//不做了，CDC口基本上是用来插手柄的
    uint8_t mute : 1;              // 静音
    uint8_t mjpegPlayMode : 2;     // 播放模式，还没做，默认单曲循环
    uint8_t : 2;                   // 占位
    uint8_t controller : 5;        // 手柄
    uint8_t : 3;                   // 占位
    uint8_t ipAddrBytes[4];        // ip地址
} SAVE_NVS_CFG;

#endif