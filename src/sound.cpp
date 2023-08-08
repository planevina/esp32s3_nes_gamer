#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/i2s.h>
#include <soc/ledc_struct.h>
#include <esp32-hal-timer.h>
#include <SD_MMC.h>

#include "main.h"
#include "emucore/emucore.h"
#include "sound.h"

typedef struct
{
    //   RIFF Section
    char RIFFSectionID[4]; // Letters "RIFF"
    uint32_t Size;         // Size of entire file less 8
    char RiffFormat[4];    // Letters "WAVE"
    //   Format Section
    char FormatSectionID[4]; // letters "fmt"
    uint32_t FormatSize;     // Size of format section less 8
    uint16_t FormatID;       // 1=uncompressed PCM
    uint16_t NumChannels;    // 1=mono,2=stereo
    uint32_t SampleRate;     // 44100, 16000, 8000 etc.
    uint32_t ByteRate;       // =SampleRate * Channels * (BitsPerSample/8)
    uint16_t BlockAlign;     // =Channels * (BitsPerSample/8) //20H~21H
    uint16_t BitsPerSample;  // 8,16,24 or 32 //22H~23H
    char mark[4];            // fact 或者LIST
    uint32_t Size2;          // DataSize or ListSize
    // Data Section
    // char DataSectionID[4];      // The letters "data"
    // uint32_t DataSize;          // Size of the data that follows
} WavHeader_Struct;

void (*audio_callback)(void *buffer, int length) = NULL;

#if SOUND_ENABLED
QueueHandle_t queue;
TaskHandle_t TASK_OPENING_SOUND_HANDLE;

static int16_t *audio_frame;

i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCK_IO,
    .ws_io_num = I2S_WS_IO,
    .data_out_num = I2S_DO_IO,
    .data_in_num = I2S_DI_IO // Not used
};
i2s_config_t audio_cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX), /*| I2S_MODE_DAC_BUILT_IN*/
    .sample_rate = DEFAULT_SAMPLERATE,    //这个是一个声道的采样率
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S | I2S_COMM_FORMAT_STAND_MSB),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    /*| ESP_INTR_FLAG_IRAM /* | ESP_INTR_FLAG_SHARED*/ ///(DO NOT USE: ESP_INTR_FLAG_IRAM = BLUETOOTH PROBLEM)
    .dma_buf_count = 2,                                // 我改了，7-8  -2
    .dma_buf_len = 512,                                // 我改了 256-1024
    .use_apll = false,
    .tx_desc_auto_clear = true,
};
void i2s_init()
{
    audio_frame = (int16_t *)heap_caps_malloc(DEFAULT_FRAGSIZE * 4, MALLOC_CAP_SPIRAM);
    i2s_driver_install(I2S_NUM_0, &audio_cfg, 2, &queue);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_zero_dma_buffer(I2S_NUM_0);
    i2s_start(I2S_NUM_0);
}
#endif

bool ValidWavData(WavHeader_Struct *Wav)
{
    if (memcmp(Wav->RIFFSectionID, "RIFF", 4) != 0)
    {
        SHOW_MSG_SERIAL("Invlaid data - Not RIFF format\n");
        return false;
    }
    if (memcmp(Wav->RiffFormat, "WAVE", 4) != 0)
    {
        SHOW_MSG_SERIAL("Invlaid data - Not Wave file\n");
        return false;
    }
    if (memcmp(Wav->FormatSectionID, "fmt", 3) != 0)
    {
        SHOW_MSG_SERIAL("Invlaid data - No format section found\n");
        return false;
    }
    if (Wav->FormatID != 1)
    {
        SHOW_MSG_SERIAL("Invlaid data - format Id must be 1\n");
        return false;
    }
    if (Wav->FormatSize != 16)
    {
        SHOW_MSG_SERIAL("Invlaid data - format section size must be 16.\n");
        return false;
    }
    if ((Wav->NumChannels != 1) & (Wav->NumChannels != 2))
    {
        SHOW_MSG_SERIAL("Invlaid data - only mono or stereo permitted.\n");
        return false;
    }
    if (Wav->SampleRate > 48000)
    {
        SHOW_MSG_SERIAL("Invlaid data - Sample rate cannot be greater than 48000\n");
        return false;
    }
    if ((Wav->BitsPerSample != 8) & (Wav->BitsPerSample != 16))
    {
        SHOW_MSG_SERIAL("Invlaid data - Only 8 or 16 bits per sample permitted.\n");
        return false;
    }
    return true;
}

int init_nes_sound(void)
{
#if SOUND_ENABLED
    //i2s_set_sample_rates(I2S_NUM_0, DEFAULT_SAMPLERATE);
    i2s_set_clk(I2S_NUM_0, DEFAULT_SAMPLERATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
#endif
    audio_callback = NULL;
    return 0;
}

void osd_setsound(void (*playfunc)(void *buffer, int length))
{
    audio_callback = playfunc; // 这个函数是将缓存读取到buffer数组供播放的
}

void osd_stopsound(void)
{
    audio_callback = NULL;
}

void osd_getsoundinfo(sndinfo_t *info)
{
    info->sample_rate = DEFAULT_SAMPLERATE;
    info->bps = 16;
}

void do_audio_frame()
{
#if SOUND_ENABLED
    if (cfg.mute)
        return;
    int left = DEFAULT_SAMPLERATE / NES_REFRESH_RATE;
    while (left)
    {
        int n = DEFAULT_FRAGSIZE;
        if (n > left)
            n = left;
        audio_callback(audio_frame, n);

        // 16 bit mono -> 32-bit (16 bit r+l)
        int16_t *mono_ptr = audio_frame + n;
        int16_t *stereo_ptr = audio_frame + n + n;
        int i = n;
        while (i--)
        {
            int16_t a = (*(--mono_ptr) >> 2);
            *(--stereo_ptr) = a;
            *(--stereo_ptr) = a;
        }
        size_t i2s_bytes_write;
        i2s_write(I2S_NUM_0, (const char *)audio_frame, 4 * n, &i2s_bytes_write, portMAX_DELAY);
        left -= i2s_bytes_write / 4;
    }
#endif
}

void task_opening_sound(void *pvParameters)
{
    File f = SD_MMC.open("/NES/opening.wav");
    if (!f)
    {
        SHOW_MSG_SERIAL("No opening sound file\n")
        vTaskDelete(NULL);
        return;
    }
    if (f.isDirectory())
    {
        SHOW_MSG_SERIAL("Opening filename is a folder\n")
        vTaskDelete(NULL);
        return;
    }
    WavHeader_Struct WavHeader;
    uint16_t wavbufsize = 1024;
    uint8_t wavBuffer[wavbufsize] = {0}; // 接收缓冲区
    f.seek(0);
    f.read(wavBuffer, 44); // 读取wav文件头
    memcpy(&WavHeader, wavBuffer, 44);
    if (!ValidWavData(&WavHeader))
    {
        SHOW_MSG_SERIAL("Failed to read wav file\n")
        vTaskDelete(NULL);
        return;
    }
    // 开始判断wave头
    uint32_t wavData_size = 0;
    if (memcmp(WavHeader.mark, "data", 4) != 0)
    {
        // 说明WAV文件有额外的信息
        // 指针需要往后跳 WavHeader.Size2;
        f.seek(f.position() + WavHeader.Size2);
        memset(wavBuffer, 0, 44);
        f.read(wavBuffer, 8);
        if (wavBuffer[0] == 0x64 && wavBuffer[1] == 0x61 && wavBuffer[2] == 0x74 && wavBuffer[3] == 0x61)
        {
            memcpy(&wavData_size, wavBuffer + 4, 4);
        }
        else
        {
            f.close();
            SHOW_MSG_SERIAL("Failed to read wav file\n")
            vTaskDelete(NULL);
            return;
        }
    }
    else
    {
        // 没有额外信息
        wavData_size = WavHeader.Size2;
    }
    //i2s_set_sample_rates(I2S_NUM_0, WavHeader.SampleRate);
    i2s_set_clk(I2S_NUM_0, WavHeader.SampleRate, WavHeader.BitsPerSample, (i2s_channel_t)WavHeader.NumChannels); //I2S_BITS_PER_SAMPLE_16BIT

    uint16_t readTimes = 0; // 需要读的次数
    size_t BytesWritten;
    TickType_t xPreviousWakeTime;
    TickType_t xDelayIncrement = pdMS_TO_TICKS(1000 / (WavHeader.ByteRate / wavbufsize));
    xPreviousWakeTime = xTaskGetTickCount();
    for (readTimes = 0; readTimes < (wavData_size / wavbufsize); readTimes++)
    {
        f.read(wavBuffer, wavbufsize);
        i2s_write(I2S_NUM_0, wavBuffer, wavbufsize, &BytesWritten, portMAX_DELAY);
        vTaskDelayUntil(&xPreviousWakeTime, xDelayIncrement);
    }

    memset(wavBuffer, 0, wavbufsize);
    f.read(wavBuffer, (wavData_size % wavbufsize));
    i2s_write(I2S_NUM_0, wavBuffer, (wavData_size % wavbufsize) + 20, &BytesWritten, portMAX_DELAY);
    f.close();
    SHOW_MSG_SERIAL("sound play over\n")
    i2s_zero_dma_buffer(I2S_NUM_0); // Clean the DMA buffer //清空缓存
    vTaskDelete(NULL);
}

void audio_mute()
{
#if SOUND_ENABLED
    i2s_zero_dma_buffer(I2S_NUM_0);
#endif
}

void switch_mute()
{
#if SOUND_ENABLED     
    cfg.mute = !cfg.mute;
    vTaskDelay(300);
    if (cfg.mute)
        audio_mute();
#endif
}
