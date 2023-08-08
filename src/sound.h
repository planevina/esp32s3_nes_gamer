#ifndef _NES_SOUND_H
#define _NES_SOUND_H

extern void (*audio_callback)(void *buffer, int length);
extern TaskHandle_t TASK_OPENING_SOUND_HANDLE;

void osd_setsound(void (*playfunc)(void *buffer, int length));
int init_nes_sound(void);

void osd_stopsound(void);
void do_audio_frame();
void i2s_init();
void audio_mute();
void switch_mute();
void task_opening_sound(void *pvParameters);

#endif