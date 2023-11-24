#include "mjpegplayer.h"
#include "main.h"
#include "MjpegClass.h"
#include "display.h"
#include <SD_MMC.h>
#include <Arduino_GFX_Library.h>

static MjpegClass mjpeg;
uint8_t *mjpeg_buf;
File playFile;
bool isReading = false;
int currFrame = 0;

#ifdef DEBUG_SHOW_MJPEG_FPS
uint32_t mjpegts = 0;
#endif

#ifdef DEBUG_SHOW_MJPEG_FPS
void showMjpegFps()
{

    SHOW_MSG_SERIAL("Frame:");
    SHOW_MSG_SERIAL(currFrame);
    SHOW_MSG_SERIAL(",ms:");
    SHOW_MSG_SERIAL(millis() - mjpegts);
    SHOW_MSG_SERIAL("\n");
}
#endif

// MJPEG解码回调函数，直接绘制
static int jpegDrawCallback(JPEGDRAW *pDraw)
{
    gfx->draw16bitBeRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
    return 1;
}

#ifdef USE_DOUBLE_BUFFER_DRAW_MJPEG
// MJPEG解码回调函数，存入缓存
static int mjpegDrawCallback1(JPEGDRAW *pDraw)
{
    for (int i = 0; i < pDraw->iHeight; i++)
    {
        memcpy(frame_odd_buf + SCREEN_RES_HOR * pDraw->y + i * SCREEN_RES_HOR + pDraw->x, pDraw->pPixels + pDraw->iWidth * i, pDraw->iWidth * 2);
    }
    return 1;
}

static int mjpegDrawCallback2(JPEGDRAW *pDraw)
{
    for (int i = 0; i < pDraw->iHeight; i++)
    {
        memcpy(frame_even_buf + SCREEN_RES_HOR * pDraw->y + i * SCREEN_RES_HOR + pDraw->x, pDraw->pPixels + pDraw->iWidth * i, pDraw->iWidth * 2);
    }
    return 1;
}
#endif

bool read_file(const char *filename)
{
    playFile = SD_MMC.open(filename, FILE_READ);
    if (!playFile || playFile.isDirectory())
    {
        return false;
    }
    return true;
}

void stopRead()
{
    if (playFile)
    {
        playFile.close();
    }
    currFrame = 0;
    isReading = false;
}

bool init_mjpeg_file(char *filename)
{
    if (!cfg.sdOK)
        return false;
    stopRead();
    bool p = false;
    char filePath[MAXFILENAME_LENGTH] = {0};
    snprintf(filePath, MAXFILENAME_LENGTH, "/mjpeg/%s.mjpeg", filename);
    if (!read_file(filePath))
    {
        return false;
    }
    currFrame = 0;

#ifdef DEBUG_SHOW_MJPEG_FPS
    mjpegts = millis();
#endif

#ifdef USE_DOUBLE_BUFFER_DRAW_MJPEG
    mjpeg.setup(&playFile, mjpeg_buf, mjpegDrawCallback1, mjpegDrawCallback2, false, 0, 0, SCREEN_RES_HOR, SCREEN_RES_VER);
#else
    mjpeg.setup(&playFile, mjpeg_buf, jpegDrawCallback, jpegDrawCallback, false, 0, 0, SCREEN_RES_HOR, SCREEN_RES_VER);
#endif

    isReading = true;
    return true;
}

void play_end_exec()
{
    // 在mjpeg循环内播放完成文件后执行的命令
#ifdef DEBUG_SHOW_MJPEG_FPS
    showMjpegFps();
#endif
    playFile.seek(0); // 回到文件开始
    currFrame = 0;
#ifdef DEBUG_SHOW_MJPEG_FPS
    mjpegts = millis();
#endif

#ifdef USE_DOUBLE_BUFFER_DRAW_MJPEG
    mjpeg.setup(&playFile, mjpeg_buf, mjpegDrawCallback1, mjpegDrawCallback2, false, 0, 0, SCREEN_RES_HOR, SCREEN_RES_VER);
#else
    mjpeg.setup(&playFile, mjpeg_buf, jpegDrawCallback, jpegDrawCallback, false, 0, 0, SCREEN_RES_HOR, SCREEN_RES_VER);
#endif

    isReading = true;
}

void mjpeg_draw_frame()
{
    // 调用一次读取一次帧
    if (isReading)
    {
        if (playFile.available() && mjpeg.readMjpegBuf())
        {
            currFrame++;
#ifdef USE_DOUBLE_BUFFER_DRAW_MJPEG
            if (currFrame % 2 == 1)
            {
                mjpeg.decodeJpg(1);
                refresh_lcd(VID_DRAW_MJPEG_ODD_FRAME);
            }
            else
            {
                mjpeg.decodeJpg(0);
                refresh_lcd(VID_DRAW_MJPEG_EVEN_FRAME);
            }
#else
            mjpeg.decodeJpg(1);
#endif
        }
        else
        {
            //  调用文件正常播放完的处理
            play_end_exec();
        }
    }
}
