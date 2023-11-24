#ifndef _HW_LCOS_PROJECTOR_H
#define _HW_LCOS_PROJECTOR_H

//=======================//
#define SCR_LCOS_HX7033

#define SCRGFX Arduino_RGB_Display

#define TFT_I2C_SDA 48
#define TFT_I2C_SCL 47

#define TFT_RGB_DE 21
#define TFT_RGB_VSYNC -1
#define TFT_RGB_HSYNC -1
#define TFT_RGB_PCLK 14

#define TFT_RGB_B0 9
#define TFT_RGB_B1 10
#define TFT_RGB_B2 11
#define TFT_RGB_B3 12
#define TFT_RGB_B4 45

#define TFT_RGB_G0 16
#define TFT_RGB_G1 17
#define TFT_RGB_G2 18
#define TFT_RGB_G3 8
#define TFT_RGB_G4 3
#define TFT_RGB_G5 46

#define TFT_RGB_R0 4
#define TFT_RGB_R1 5
#define TFT_RGB_R2 6
#define TFT_RGB_R3 7
#define TFT_RGB_R4 15

#define BTN_SCHE_TWOBTN
#define BTN_B_PIN 0
#define BTN_A_PIN 42

#define TFT_BL 38
#define TFT_BLK_PWM_FREQ

#define USE_1BIT_SDMMC true
#define SD_MMC_D0_PIN 39
#define SD_MMC_D1_PIN -1
#define SD_MMC_D2_PIN -1
#define SD_MMC_D3_PIN -1
#define SD_MMC_CLK_PIN 40
#define SD_MMC_CMD_PIN 41


#define SOUND_ENABLED true

// AUDIO_i2S
#define I2S_BCK_IO 1 // BCK
#define I2S_WS_IO 2  // LCK
#define I2S_DO_IO 13  // DIN
#define I2S_DI_IO (-1)

#define SCREEN_RES_HOR 320
#define SCREEN_RES_VER 240

#endif