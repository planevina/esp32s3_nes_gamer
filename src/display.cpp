#include "config.h"
#include "display.h"
#include <Arduino_GFX_Library.h>
#include "emucore/nes_palette.h"

#ifdef USE_DOUBLE_BUFFER_DRAW_MJPEG
uint16_t *frame_odd_buf;
uint16_t *frame_even_buf;
#endif

static uint16_t frameCacheStart = 0;                             // NES刷屏，从缓存数组第几位开始刷
static uint16_t screenXStart = 0;                                // NES刷屏的x轴起始点
static uint16_t linePixels = NES_SCREEN_WIDTH;                   // NES刷屏每一行的像素
static uint16_t frameHeight = NES_SCREEN_HEIGHT;                 // NES刷屏高度
static uint16_t screenYStart = 0;                                // NES刷屏的y轴起始点
static uint16_t frameCacheRowOffset = 0;                         // NES刷屏，缓存数组跳过的行
const uint32_t frameBytes = SCREEN_RES_HOR * SCREEN_RES_VER * 2; // 全屏所需要的字节
uint8_t *SCREENMEMORY = NULL;                                    // NES屏幕缓存
TaskHandle_t TASK_VID_HANDLE;

#ifdef SCR_LCOS_HX7033
#include <Wire.h>

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    TFT_RGB_DE, TFT_RGB_VSYNC, TFT_RGB_HSYNC, TFT_RGB_PCLK,
    TFT_RGB_B0, TFT_RGB_B1, TFT_RGB_B2, TFT_RGB_B3, TFT_RGB_B4,
    TFT_RGB_G0, TFT_RGB_G1, TFT_RGB_G2, TFT_RGB_G3, TFT_RGB_G4, TFT_RGB_G5,
    TFT_RGB_R0, TFT_RGB_R1, TFT_RGB_R2, TFT_RGB_R3, TFT_RGB_R4,
    1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 80 /* hsync_back_porch */,
    1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 60 /* vsync_back_porch */);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    SCREEN_RES_HOR, SCREEN_RES_VER, rgbpanel, 0 /* rotation */, true /* auto_flush */);

#define LCOS_ADDRESS 0x48

bool checkI2CDevice(int address)
{
    Wire.beginTransmission(address);
    return (Wire.endTransmission() == 0);
}

static void i2cwrite(uint8_t addr, uint8_t val)
{
    Wire.beginTransmission(LCOS_ADDRESS);
    Wire.write(addr);
    Wire.write(val);
    Wire.endTransmission();
}

void display_init()
{
    SHOW_MSG_SERIAL("Initializing RGB Panel...")
    Wire.begin(TFT_I2C_SDA, TFT_I2C_SCL);
    Wire.setClock(100000);
    if (checkI2CDevice(LCOS_ADDRESS))
    {
        i2cwrite(0x00, 0b01010110); // R00h
        i2cwrite(0x0A, 0xF2);
        i2cwrite(0x0B, 0x80);
        i2cwrite(0x0C, 0x00);
        i2cwrite(0x0D, 0x51);
        i2cwrite(0x0E, 0x8A);
        i2cwrite(0x0F, 0xB2);
        i2cwrite(0x10, 0xD8);
        i2cwrite(0x11, 0xE5);
        i2cwrite(0x12, 0x19);
        i2cwrite(0x13, 0x26);
        i2cwrite(0x14, 0x4C);
        i2cwrite(0x15, 0x74);
        i2cwrite(0x16, 0xAD);
        i2cwrite(0x17, 0xFF);
    }
    else
    {
        SHOW_MSG_SERIAL("I2C Fail...");
    }

    gfx->begin(14400000L);
    gfx->fillScreen(BLACK);
    gfx->setTextColor(UI_TEXT_COLOR);
#ifdef TFT_BL
    pinMode(TFT_BL, OUTPUT);
#ifdef TFT_BLK_ON_LOW
    digitalWrite(TFT_BL, LOW);
#else
    digitalWrite(TFT_BL, HIGH);
#endif
#endif
    SHOW_MSG_SERIAL("[ OK ]\n")
}

#else
Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, TFT_MISO, HSPI);

SCRGFX *gfx = new SCRGFX(bus, TFT_RST, TFT_ROTATION, TFT_IS_IPS);

void display_init()
{
    gfx->begin(80000000L);
#ifdef HOLOCUBIC_PLUS_BOARD
    // 镜像+旋转
    uint8_t r = ST7789_MADCTL_MX | ST7789_MADCTL_RGB;
    bus->beginWrite();
    bus->writeCommand(ST7789_MADCTL);
    bus->write(r);
    bus->endWrite();
#endif
    gfx->fillScreen(BLACK);
    gfx->setTextColor(UI_TEXT_COLOR);

#ifdef TFT_BL
    pinMode(TFT_BL, OUTPUT);
#ifdef TFT_BLK_ON_LOW
    digitalWrite(TFT_BL, LOW);
#else
    digitalWrite(TFT_BL, HIGH);
#endif

#endif
}

#endif

void refresh_lcd(vid_notify_command_t cmd)
{
    xTaskNotify(TASK_VID_HANDLE, cmd, eSetValueWithOverwrite);
}

static void lcd_write_frame(uint32_t target)
{
#ifdef USE_DOUBLE_BUFFER_DRAW_MJPEG
    if (target == VID_DRAW_NES_FRAME)
    {
        // 刷新NES帧
#ifdef SCR_LCOS_HX7033
        gfx->drawIndexedBitmap(screenXStart, frameCacheRowOffset, SCREENMEMORY, (uint16_t *)nes_palette_256, linePixels, frameHeight);
#else
        gfx->startWrite();
        gfx->writeAddrWindow(screenXStart, 0, linePixels, frameHeight);
        for (int32_t i = 0; i < frameHeight; i++)
        {
            gfx->writeIndexedPixels(SCREENMEMORY + NES_SCREEN_WIDTH * (i + frameCacheRowOffset) + frameCacheStart, (uint16_t *)nes_palette_256, linePixels);
        }
        gfx->endWrite();
#endif
    }
    else if (target == VID_DRAW_MJPEG_ODD_FRAME)
    {
        // 绘制MJPEG单帧
        gfx->draw16bitRGBBitmap(0, 0, frame_odd_buf, SCREEN_RES_HOR, SCREEN_RES_VER);
    }
    else if (target == VID_DRAW_MJPEG_EVEN_FRAME)
    {
        // 绘制MJPEG双帧
        gfx->draw16bitRGBBitmap(0, 0, frame_even_buf, SCREEN_RES_HOR, SCREEN_RES_VER);
    }
#else
    // 只有NES会用到
    // 刷新NES帧
#ifdef SCR_LCOS_HX7033
    gfx->drawIndexedBitmap(screenXStart, frameCacheRowOffset, SCREENMEMORY, (uint16_t *)nes_palette_256, linePixels, frameHeight);
#else
    gfx->startWrite();
    gfx->writeAddrWindow(screenXStart, 0, linePixels, frameHeight);
    for (int32_t i = 0; i < frameHeight; i++)
    {
        gfx->writeIndexedPixels(SCREENMEMORY + NES_SCREEN_WIDTH * (i + frameCacheRowOffset) + frameCacheStart, (uint16_t *)nes_palette_256, linePixels);
    }
    gfx->endWrite();
#endif
#endif
}

void videoTask(void *arg)
{
    // 根据各种屏幕计算必须的参数
    if (SCREEN_RES_HOR < 480)
    {
        // 分辨率 240x240 或者 320x240
        if (SCREEN_RES_HOR > NES_SCREEN_WIDTH)
        {
            // 主要是320x240的情况
            screenXStart = (SCREEN_RES_HOR - NES_SCREEN_WIDTH) / 2;
            frameCacheStart = 0;
            linePixels = NES_SCREEN_WIDTH;
        }
        else
        {
            // 主要是240x240的情况
            screenXStart = 0;
            frameCacheStart = (NES_SCREEN_WIDTH - SCREEN_RES_HOR) / 2;
            linePixels = SCREEN_RES_HOR;
        }
    }
    else
    {
        // 屏幕更大的情况，仍然显示在中间
        screenXStart = (SCREEN_RES_HOR - NES_SCREEN_WIDTH) / 2;
        frameCacheStart = 0;
        linePixels = NES_SCREEN_WIDTH;
    }
    if (SCREEN_RES_VER < NES_SCREEN_HEIGHT)
    {
        // 屏幕高度小于NES默认高度240
        frameHeight = SCREEN_RES_VER;
        screenYStart = 0;
        frameCacheRowOffset = (NES_SCREEN_HEIGHT - SCREEN_RES_VER) / 2;
    }
    else
    {
        // 屏幕高度大于等于NES默认高度，刷新在中间
        frameHeight = NES_SCREEN_HEIGHT;
        screenYStart = (SCREEN_RES_VER - NES_SCREEN_HEIGHT) / 2;
        frameCacheRowOffset = 0;
    }
    uint32_t taskNotifyCommand = 0;
    BaseType_t xResult;
    while (1)
    {
        xResult = xTaskNotifyWait(0x00, ULONG_MAX, &taskNotifyCommand, portMAX_DELAY);
        if (xResult == pdPASS)
        {
            lcd_write_frame(taskNotifyCommand);
        }
    }
}