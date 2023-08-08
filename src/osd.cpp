#include "osd.h"
#include "display.h"
#include "ui.h"
#include "network.h"
#include "sound.h"
#include "nvstools.h"
#include "emucore/emucore.h"
#include "indev/controller.h"
#include "pc_monitor/pc_monitor.h"
#include "mjpeg/mjpegplayer.h"
#include "fonts/fontPressStart2P.h"
#include <FFat.h>
#include <SD_MMC.h>

FILE_OBJ *romFiles = NULL;                         // rom文件列表
uint8_t *cachedRom = NULL;                         // 读取到PSRAM中的rom
uint8_t romReadingBuffer[ROM_READING_BUFFER_SIZE]; // Rom读取缓存
FILE_OBJ *mjpegFiles = NULL;                       // mjpeg文件列表
uint32_t tickCount = 0;                            // 用于在游戏每一帧之间控制延时
TimerHandle_t timer_1 = NULL;                      // 定时器,在NES中用于音频，在AIDA64中用于更新
uint32_t lastPcUpdateTs = 0;                       // 最后一次更新PC状态的时间戳

#ifdef BAT_ADC_PIN
int8_t curr_batt_level = -1; // 当前的电量状态
uint32_t last_batt_ts = 0;   // 最后一次更新电池状态的时间戳
uint32_t batt_ts = 0;        // 临时变量
#endif

void stop_timer()
{
    if (NULL != timer_1)
    {
        xTimerDelete(timer_1, 0);
        timer_1 = NULL;
    }
}

IRAM_ATTR static void timer_isr(void)
{
    if (audio_callback && cfg.nesPower && !cfg.mute)
        do_audio_frame();
}

IRAM_ATTR int start_nes_audio_timer(int hertz)
{
    stop_timer();
    timer_1 = xTimerCreate("nes_audio", configTICK_RATE_HZ / hertz, pdTRUE, NULL, (TimerCallbackFunction_t)timer_isr);
    xTimerStart(timer_1, 0);
    return 0;
}

void sortFiles(FILE_OBJ *arr, int n)
{
    FILE_OBJ temp;
    for (int j = 0; j < n - 1; j++)
    {
        for (int i = j + 1; i < n; i++)
        {
            if (strcmp(arr[j].file_name, arr[i].file_name) > 0)
            {
                // Swap the entire FILE_OBJ structure
                temp = arr[j];
                arr[j] = arr[i];
                arr[i] = temp;
            }
        }
    }
}

uint8_t get_batt_level()
{
    uint8_t bat_level = 0;
#ifdef BAT_ADC_PIN
    uint32_t bat_ad_v = 2 * analogReadMilliVolts(BAT_ADC_PIN); // 读取电压值
    log_d("AD_V = %dmV", bat_ad_v);

    if (bat_ad_v > 4000)
    {
        bat_level = 4;
    }
    else if (bat_ad_v > 3900)
    {
        bat_level = 3;
    }
    else if (bat_ad_v > 3800)
    {
        bat_level = 2;
    }
    else if (bat_ad_v > 3700)
    {
        bat_level = 1;
    }
#endif
    return bat_level;
}

///////////////////////////////////////////////////
// NES模拟器部分===================================
///////////////////////////////////////////////////

// 在游戏中快捷组合键判定
void shortcut_nes()
{
    // 在游戏中只响应两个快捷键
    // SELECT + START  退出
    // SELECT + B 静音
    if ((gamepad_p1.JOY_START && gamepad_p1.JOY_SELECT) && !gamepad_p1.JOY_A && !gamepad_p1.JOY_B)
    {
        // 返回选游戏界面
        cfg.nesPower = 0;
        input_clear();
        vTaskDelay(500);
    }
#if SOUND_ENABLED
    else if (gamepad_p1.JOY_SELECT && gamepad_p1.JOY_B)
    {
        // 切换静音
        switch_mute();
        input_clear();
        vTaskDelay(200);
    }
#endif
}

bool list_nes_file()
{
    if (cfg.romFilenameCached)
        return true;

    SHOW_MSG_SERIAL("Listing files...")
    uint8_t num = 0;
    cfg.loadedRomFileNames = 0;
    File root = SD_MMC.open("/NES");
    if (!root)
    {
        draw_no_tf_card(1, 2);
        STAY_HERE
    }
    if (!root.isDirectory())
    {
        draw_no_tf_card(1, 2);
        STAY_HERE
    }
    File file = root.openNextFile();

    gfx->fillRect(10, 220, 96, 8, 0);
    gfx->setFont(PIXEL_FONT_X1);
    gfx->setCursor(UI_STATUSBAR_CURSOR_X, UI_STATUSBAR_CURSOR_Y);
    gfx->printf("Found games:");
    while (file && num < MAXFILES)
    {
        if (!file.isDirectory())
        {
            memset(romFiles[num].file_name, 0, MAXFILENAME_LENGTH);
            int len = snprintf(romFiles[num].file_name, MAXFILENAME_LENGTH, "%s", file.name());
            if (len > 4 && len < MAXFILENAME_LENGTH) //".nes"一共4位，长度至少5才合理
            {
                if ((romFiles[num].file_name[len - 4] == '.') && (romFiles[num].file_name[len - 3] == 'N' || romFiles[num].file_name[len - 3] == 'n') && (romFiles[num].file_name[len - 2] == 'E' || romFiles[num].file_name[len - 2] == 'e') && (romFiles[num].file_name[len - 1] == 'S' || romFiles[num].file_name[len - 1] == 's'))
                {
                    romFiles[num].file_name[len - 1] = '\0';
                    romFiles[num].file_name[len - 2] = '\0';
                    romFiles[num].file_name[len - 3] = '\0';
                    romFiles[num].file_name[len - 4] = '\0';
                    romFiles[num].file_size = (uint16_t)(file.size() / 1024);
                    num++;
                    gfx->fillRect(106, 220, 32, 8, 0);
                    gfx->setCursor(106, UI_STATUSBAR_CURSOR_Y);
                    gfx->print(num);
                }
            }
        }
        cfg.loadedRomFileNames = num;
        file = root.openNextFile();
    }
    file.close();
    root.close();
    SHOW_MSG_SERIAL("[ OK ],Loaded files:")
    SHOW_MSG_SERIAL(cfg.loadedRomFileNames)
    SHOW_MSG_SERIAL("\n")
    sortFiles(romFiles, cfg.loadedRomFileNames);
    cfg.romFilenameCached = 1;
    return true;
}

char *NESMENU()
{
    draw_game_list_ui(cfg.loadedRomFileNames);
    // 显示文件名
    cfg.menuCursor = 0;
    cfg.menuPage = 0;
    bool NamesDisplayed = false;
    while (1)
    {
        cfg.menuPage = cfg.menuCursor / FILES_PER_PAGE;
        if (!NamesDisplayed)
        {
            clear_nes_gamelist_area();
            for (uint8_t num = cfg.menuPage * FILES_PER_PAGE; num < ((cfg.menuPage + 1) * FILES_PER_PAGE) && num < cfg.loadedRomFileNames; num++)
            {
                draw_nes_game_item(romFiles[num].file_name, (num % FILES_PER_PAGE), cfg.menuPage, cfg.menuCursor == num, cfg.loadedRomFileNames);
            }
            draw_game_left_pane(cfg.menuCursor % 5, cfg.loadedRomFileNames);
            NamesDisplayed = true;
            draw_file_size(romFiles[cfg.menuCursor].file_size, MY_APP_NES);
        }

        // 获取按键状态，按键状态是靠后台TASK更新全局变量
        input_refresh();

        // 菜单界面单键判定
        if (gamepad_p1.JOY_START)
        {
            SHOW_MSG_SERIAL("Set auto play ...")
            if (save_auto_play(romFiles[cfg.menuCursor].file_name, MY_APP_NES))
            {
                SHOW_MSG_SERIAL("[ OK ]\n")
            }
            else
            {
                SHOW_MSG_SERIAL("[ FAIL ]\n")
            }
            input_clear();
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS2);
        }
        else if (gamepad_p1.JOY_SELECT)
        {
            SHOW_MSG_SERIAL("Clear auto play...")
            if (clear_auto_play(MY_APP_NES))
            {
                SHOW_MSG_SERIAL("[ OK ]\n")
            }
            else
            {
                SHOW_MSG_SERIAL("[ FAIL ]\n")
            }
            input_clear();
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS2);
        }
        else if (gamepad_p1.JOY_UP)
        {
            if (cfg.menuCursor % FILES_PER_PAGE == 0)
                NamesDisplayed = false; // 翻页
            if (cfg.menuCursor == 0 && cfg.loadedRomFileNames > 0)
            {
                cfg.menuCursor = cfg.loadedRomFileNames - 1;
                // 这里已经触发了上面的重绘
            }
            else if (cfg.menuCursor > 0 && cfg.loadedRomFileNames > 0)
            {
                draw_nes_game_item(romFiles[cfg.menuCursor].file_name, cfg.menuCursor % FILES_PER_PAGE, cfg.menuPage, false, cfg.loadedRomFileNames);
                cfg.menuCursor--;
                draw_nes_game_item(romFiles[cfg.menuCursor].file_name, cfg.menuCursor % FILES_PER_PAGE, cfg.menuPage, true, cfg.loadedRomFileNames);
                draw_cart_logo(cfg.menuCursor % 5);
                draw_file_size(romFiles[cfg.menuCursor].file_size, MY_APP_NES);
            }
            gamepad_p1.JOY_UP = 0;
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
        }
        if (gamepad_p1.JOY_DOWN)
        {
            if (cfg.menuCursor % FILES_PER_PAGE == FILES_PER_PAGE - 1 || cfg.menuCursor == cfg.loadedRomFileNames - 1)
                NamesDisplayed = false; // 翻页
            if (cfg.menuCursor == cfg.loadedRomFileNames - 1 && cfg.loadedRomFileNames > 0)
                cfg.menuCursor = 0; // 这里已经触发了上面的重绘
            else if (cfg.menuCursor < cfg.loadedRomFileNames - 1 && cfg.loadedRomFileNames > 0)
            {
                draw_nes_game_item(romFiles[cfg.menuCursor].file_name, cfg.menuCursor % FILES_PER_PAGE, cfg.menuPage, false, cfg.loadedRomFileNames);
                cfg.menuCursor++;
                draw_nes_game_item(romFiles[cfg.menuCursor].file_name, cfg.menuCursor % FILES_PER_PAGE, cfg.menuPage, true, cfg.loadedRomFileNames);
                draw_cart_logo(cfg.menuCursor % 5);
                draw_file_size(romFiles[cfg.menuCursor].file_size, MY_APP_NES);
            }
            gamepad_p1.JOY_DOWN = 0;
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
        }
        if (gamepad_p1.JOY_LEFT)
        {
            if (cfg.menuCursor > FILES_PER_PAGE - 1)
                cfg.menuCursor -= FILES_PER_PAGE;
            NamesDisplayed = false;
            gamepad_p1.JOY_LEFT = 0;
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
        }
        if (gamepad_p1.JOY_RIGHT)
        {
            if (cfg.menuCursor / FILES_PER_PAGE < cfg.loadedRomFileNames / FILES_PER_PAGE)
                cfg.menuCursor += FILES_PER_PAGE;
            if (cfg.menuCursor > cfg.loadedRomFileNames - 1)
                cfg.menuCursor = cfg.loadedRomFileNames - 1;
            NamesDisplayed = false;
            gamepad_p1.JOY_RIGHT = 0;
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
        }
        if (gamepad_p1.JOY_B)
        {
            // 退出NES模块
            gamepad_p1.JOY_B = 0;
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
            return NULL;
        }
        if (gamepad_p1.JOY_A && gamepad_p1.JOY_SELECT == 0)
        {
            gamepad_p1.JOY_A = 0;
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS2);
            return romFiles[cfg.menuCursor].file_name;
        }
        vTaskDelay(10);
    };
}

//================================================================================
// 重写的rom读取函数，读取到psram
//================================================================================
int getromdata(char *ROMFILENAME_)
{
    char namebuffer[MAXFILENAME_LENGTH] = {0};
    snprintf(namebuffer, sizeof(namebuffer), "%s%s.nes", NES_FOLDER, ROMFILENAME_);
    File fp = SD_MMC.open(namebuffer);
    if (!fp || fp.isDirectory())
    {
        SHOW_MSG_SERIAL("<File error>")
        return -1;
    }
    SHOW_MSG_SERIAL("size:")
    SHOW_MSG_SERIAL(fp.size())
    SHOW_MSG_SERIAL("...")

    memset(cachedRom, 0, cfg.romMaxSize); // 清零
    uint32_t i = 0;
    while (fp.available())
    {
        memset(romReadingBuffer, 0, ROM_READING_BUFFER_SIZE);
        size_t bytesRead = fp.read(romReadingBuffer, ROM_READING_BUFFER_SIZE);
        memcpy(cachedRom + i, romReadingBuffer, bytesRead);
        i += bytesRead;
    }
    fp.close();
    return 0;
}

void nes_loop()
{
    // 从这里开始是NES模拟器部分
    if (!cfg.sdOK)
    {
        draw_no_tf_card(1, 1);
        STAY_HERE
    }
    cfg.menuCursor = 0;
    cfg.menuPage = 0;
    clear_screen(false);
    if (!list_nes_file())
    {
        cfg.currMode = MY_APP_NONE;
        return; // 退出
    }

#if SOUND_ENABLED
    init_nes_sound();
    start_nes_audio_timer(NES_REFRESH_RATE);
#endif

    clear_screen(false);
    reset_statusbar();
    char *selectedFilename = NULL;
    char filePath[MAXFILENAME_LENGTH] = {0};
    // 判断自动游戏
    if (!cfg.nesEnterFlag)
    {
        // 仅当第一次进入该模式才执行
        if (load_auto_play(filePath, MY_APP_NES))
        {
            // 判断读出来的文件名是否存在
            for (int i = 0; i < cfg.loadedRomFileNames; i++)
            {
                if (strcmp(romFiles[i].file_name, filePath) == 0)
                {
                    // 存在
                    selectedFilename = filePath;
                    SHOW_MSG_SERIAL("Auto loading game: '")
                    SHOW_MSG_SERIAL(selectedFilename)
                    SHOW_MSG_SERIAL("'\n")
                    break;
                }
            }
            if (NULL == selectedFilename)
            {
                clear_auto_play(MY_APP_NES);
                vTaskDelay(200);
            }
        }
        cfg.nesEnterFlag = 1;
    }

    while (1)
    {
        if (NULL == selectedFilename)
        {
            selectedFilename = NESMENU();
            // 如果还是空值，退出并返回主界面
            if (NULL == selectedFilename)
            {
                vTaskDelay(200);
#if 0
                //退出的时候不能删除nes对象，否则多次切换会出错
                if (NESmachine->rominfo != NULL)
                {
                    free(NESmachine->rominfo);
                    NESmachine->rominfo = NULL;
                }
                nes_destroy(&NESmachine);
#endif
                stop_timer();
                cfg.currMode = MY_APP_NONE;
                return; // 退出
            }
        }

        clear_screen(false);
        reset_statusbar();
        SHOW_MSG_BOTH("Loading rom: '")
        SHOW_MSG_BOTH(selectedFilename)
        SHOW_MSG_BOTH("' in psram...")

        if (getromdata(selectedFilename))
        {
            SHOW_MSG_BOTH("[ FAIL ]\n")
            selectedFilename = NULL;
            vTaskDelay(10000);
            continue; // 读取ROM file失败，等待10秒后返回选择界面
        }
        uint8_t *romdata = NULL;
        romdata = cachedRom; // romdata每次会被释放，不能直接将rom_cache_psram作为参数传递

        SHOW_MSG_BOTH("[ OK ]\n")
        reset_statusbar();
        SHOW_MSG_BOTH("Analysing...")

        memset(NESmachine->rominfo, 0, sizeof(rominfo_t));

        // 获取ROM头信息并存入rominfo结构体
        if (rom_getheader(&romdata, NESmachine->rominfo))
        {
            SHOW_MSG_BOTH("[ FAIL ]\n")
            selectedFilename = NULL;
            vTaskDelay(10000);
            continue; // 获取rom头文件出错，等待10秒后返回选择界面
        }

        // 确认是否支持这个游戏的Mapper
        if (false == mmc_peek(NESmachine->rominfo->mapper_number))
        {
            SHOW_MSG_BOTH("Rom is not supported:")
            SHOW_MSG_BOTH(NESmachine->rominfo->mapper_number)
            SHOW_MSG_BOTH("[ FAIL ]\n")
            selectedFilename = NULL;
            vTaskDelay(10000);
            continue; // 不支持这个游戏，等待10秒后返回选择界面
        }

        if (rom_allocsram(&(NESmachine->rominfo)))
        {
            SHOW_MSG_BOTH("[ FAIL ]\n")
            selectedFilename = NULL;
            vTaskDelay(10000);
            continue; // sram分配出错，等待10秒后返回选择界面
        }

        rom_loadtrainer(&romdata, NESmachine->rominfo);

        if (rom_loadrom(&romdata, NESmachine->rominfo))
        {
            SHOW_MSG_BOTH("[ FAIL ]\n")
            selectedFilename = NULL;
            vTaskDelay(10000);
            continue; // rom读取出错，等待10秒后返回选择界面
        }

        SHOW_MSG_BOTH("[ OK ]\n")
        reset_statusbar();
        SHOW_MSG_BOTH("Inserting Cartridge...")

        // map cart's SRAM to CPU $6000-$7FFF
        if (NESmachine->rominfo->sram)
        {
            NESmachine->cpu->mem_page[6] = NESmachine->rominfo->sram;
            NESmachine->cpu->mem_page[7] = NESmachine->rominfo->sram + 0x1000;
        }

        // mapper
        NESmachine->mmc = mmc_create(NESmachine->rominfo);
        if (NULL == NESmachine->mmc)
        {
            SHOW_MSG_BOTH("[ FAIL ]\n")
            selectedFilename = NULL;
            vTaskDelay(10000);
            continue; // mapper创建出错，等待10秒后返回选择界面
        }
        // 分配VRAM
        if (NULL != NESmachine->rominfo->vram)
            NESmachine->ppu->vram_present = true;
        if (SOUND_ENABLED)
        {
            apu_setext(NESmachine->apu, NESmachine->mmc->intf->sound_ext);
            osd_setsound(NESmachine->apu->process);
        }
        build_address_handlers(NESmachine);
        nes_setcontext(NESmachine);
        nes_reset();
        cfg.nesPower = 1;

        SHOW_MSG_BOTH("[ OK ]\n")

        clear_screen(false);

        while (cfg.nesPower == 1)
        {
            tickCount = micros();

#ifdef BAT_ADC_PIN
            batt_ts = tickCount / 1000;
            if (batt_ts > last_batt_ts)
            {
                last_batt_ts = batt_ts + 5000;
                draw_batt_level(get_batt_level());
            }
#endif
            input_clear();
            input_refresh();
            nes_renderframe(true);
            refresh_lcd(VID_DRAW_NES_FRAME);
            tickCount = micros() - tickCount;
            if (tickCount < 1000000 / NES_REFRESH_RATE)
                vTaskDelay(1000 / NES_REFRESH_RATE - tickCount / 1000);
            shortcut_nes();
        }
        vTaskDelay(500);
        osd_stopsound();
        audio_mute();

        if (NESmachine->rominfo->sram != NULL)
        {
            free(NESmachine->rominfo->sram);
            NESmachine->rominfo->sram == NULL;
        }
        selectedFilename = NULL;
    }
}
///////////////////////////////////////////////////
// NES模拟器部分结束===================================
///////////////////////////////////////////////////

///////////////////////////////////////////////////
// MJPEG部分开始===================================
///////////////////////////////////////////////////
bool list_mjpeg_file()
{
    if (cfg.mjpegFilenameCached)
        return true;
    SHOW_MSG_SERIAL("Listing files...")
    uint8_t num = 0;
    cfg.loadedMjpegFileNames = 0;
    File root = SD_MMC.open("/mjpeg");
    if (!root)
    {
        draw_no_tf_card(0, 2);
        STAY_HERE
    }
    if (!root.isDirectory())
    {
        draw_no_tf_card(0, 2);
        STAY_HERE
    }
    File file = root.openNextFile();
    gfx->fillRect(10, 220, 96, 8, 0);
    gfx->setFont(PIXEL_FONT_X1);
    gfx->setCursor(UI_STATUSBAR_CURSOR_X, UI_STATUSBAR_CURSOR_Y);
    gfx->printf("Found mjpeg:");

    while (file && num < MAXFILES)
    {
        if (!file.isDirectory())
        {
            memset(mjpegFiles[num].file_name, 0, MAXFILENAME_LENGTH);
            int len = snprintf(mjpegFiles[num].file_name, MAXFILENAME_LENGTH, "%s", file.name());
            if (len > 6 && len < MAXFILENAME_LENGTH) //".mjpeg"一共6位，长度至少7才合理
            {
                if ((mjpegFiles[num].file_name[len - 6] == '.') && (mjpegFiles[num].file_name[len - 5] == 'M' || mjpegFiles[num].file_name[len - 5] == 'm') && (mjpegFiles[num].file_name[len - 4] == 'J' || mjpegFiles[num].file_name[len - 4] == 'j') && (mjpegFiles[num].file_name[len - 3] == 'P' || mjpegFiles[num].file_name[len - 3] == 'p') && (mjpegFiles[num].file_name[len - 2] == 'E' || mjpegFiles[num].file_name[len - 2] == 'e') && (mjpegFiles[num].file_name[len - 1] == 'G' || mjpegFiles[num].file_name[len - 1] == 'g'))
                {
                    mjpegFiles[num].file_name[len - 1] = '\0';
                    mjpegFiles[num].file_name[len - 2] = '\0';
                    mjpegFiles[num].file_name[len - 3] = '\0';
                    mjpegFiles[num].file_name[len - 4] = '\0';
                    mjpegFiles[num].file_name[len - 5] = '\0';
                    mjpegFiles[num].file_name[len - 6] = '\0';
                    mjpegFiles[num].file_size = (uint16_t)(file.size() / 1048576); // MB
                    num++;
                    gfx->fillRect(106, 220, 32, 8, 0);
                    gfx->setCursor(106, UI_STATUSBAR_CURSOR_Y);
                    gfx->print(num);
                }
            }
        }
        cfg.loadedMjpegFileNames = num;
        file = root.openNextFile();
    }
    file.close();
    root.close();
    SHOW_MSG_SERIAL("[ OK ],Loaded mjpeg files:")
    SHOW_MSG_SERIAL(cfg.loadedMjpegFileNames)
    SHOW_MSG_SERIAL("\n")
    sortFiles(mjpegFiles, cfg.loadedMjpegFileNames);
    cfg.mjpegFilenameCached = 1;
    return true;
}
char *MJPEGMENU()
{
    draw_mjpeg_list_ui(cfg.loadedMjpegFileNames);
    // 显示文件名
    cfg.menuCursor = 0;
    cfg.menuPage = 0;
    bool NamesDisplayed = false;
    while (1)
    {
        cfg.menuPage = cfg.menuCursor / FILES_PER_PAGE;
        if (!NamesDisplayed)
        {
            clear_mjpeg_filelist_area();
            for (uint8_t num = cfg.menuPage * FILES_PER_PAGE; num < ((cfg.menuPage + 1) * FILES_PER_PAGE) && num < cfg.loadedMjpegFileNames; num++)
            {
                draw_mjpeg_file_item(mjpegFiles[num].file_name, (num % FILES_PER_PAGE), cfg.menuPage, cfg.menuCursor == num, cfg.loadedMjpegFileNames);
            }
            draw_file_size(mjpegFiles[cfg.menuCursor].file_size, MY_APP_MJPEG);
            NamesDisplayed = true;
        }

        // 获取按键状态，按键状态是靠后台TASK更新全局变量
        input_refresh();

        // 菜单界面单键判定
        if (gamepad_p1.JOY_START)
        {
            SHOW_MSG_SERIAL("Set auto play...")
            if (save_auto_play(mjpegFiles[cfg.menuCursor].file_name, MY_APP_MJPEG))
            {
                SHOW_MSG_SERIAL("[ OK ]\n")
            }
            else
            {
                SHOW_MSG_SERIAL("[ FAIL ]\n")
            }
            input_clear();
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS2);
        }
        else if (gamepad_p1.JOY_SELECT)
        {
            SHOW_MSG_SERIAL("Clear auto play...")
            if (clear_auto_play(MY_APP_MJPEG))
            {
                SHOW_MSG_SERIAL("[ OK ]\n")
            }
            else
            {
                SHOW_MSG_SERIAL("[ FAIL ]\n")
            }
            input_clear();
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS2);
        }
        else if (gamepad_p1.JOY_UP)
        {
            if (cfg.menuCursor % FILES_PER_PAGE == 0)
                NamesDisplayed = false; // 翻页
            if (cfg.menuCursor == 0 && cfg.loadedMjpegFileNames > 0)
            {
                cfg.menuCursor = cfg.loadedMjpegFileNames - 1;
                // 这里已经触发了上面的重绘
            }
            else if (cfg.menuCursor > 0 && cfg.loadedMjpegFileNames > 0)
            {
                draw_mjpeg_file_item(mjpegFiles[cfg.menuCursor].file_name, cfg.menuCursor % FILES_PER_PAGE, cfg.menuPage, false, cfg.loadedMjpegFileNames);
                cfg.menuCursor--;
                draw_mjpeg_file_item(mjpegFiles[cfg.menuCursor].file_name, cfg.menuCursor % FILES_PER_PAGE, cfg.menuPage, true, cfg.loadedMjpegFileNames);
                draw_file_size(mjpegFiles[cfg.menuCursor].file_size, MY_APP_MJPEG);
            }
            gamepad_p1.JOY_UP = 0;
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
        }
        if (gamepad_p1.JOY_DOWN)
        {
            if (cfg.menuCursor % FILES_PER_PAGE == FILES_PER_PAGE - 1 || cfg.menuCursor == cfg.loadedMjpegFileNames - 1)
                NamesDisplayed = false; // 翻页
            if (cfg.menuCursor == cfg.loadedMjpegFileNames - 1 && cfg.loadedMjpegFileNames > 0)
                cfg.menuCursor = 0; // 这里已经触发了上面的重绘
            else if (cfg.menuCursor < cfg.loadedMjpegFileNames - 1 && cfg.loadedMjpegFileNames > 0)
            {
                draw_mjpeg_file_item(mjpegFiles[cfg.menuCursor].file_name, cfg.menuCursor % FILES_PER_PAGE, cfg.menuPage, false, cfg.loadedMjpegFileNames);
                cfg.menuCursor++;
                draw_mjpeg_file_item(mjpegFiles[cfg.menuCursor].file_name, cfg.menuCursor % FILES_PER_PAGE, cfg.menuPage, true, cfg.loadedMjpegFileNames);
                draw_file_size(mjpegFiles[cfg.menuCursor].file_size, MY_APP_MJPEG);
            }
            gamepad_p1.JOY_DOWN = 0;
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
        }
        if (gamepad_p1.JOY_LEFT)
        {
            if (cfg.menuCursor > FILES_PER_PAGE - 1)
                cfg.menuCursor -= FILES_PER_PAGE;
            NamesDisplayed = false;
            gamepad_p1.JOY_LEFT = 0;
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
        }
        if (gamepad_p1.JOY_RIGHT)
        {
            if (cfg.menuCursor / FILES_PER_PAGE < cfg.loadedMjpegFileNames / FILES_PER_PAGE)
                cfg.menuCursor += FILES_PER_PAGE;
            if (cfg.menuCursor > cfg.loadedMjpegFileNames - 1)
                cfg.menuCursor = cfg.loadedMjpegFileNames - 1;
            NamesDisplayed = false;
            gamepad_p1.JOY_RIGHT = 0;
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
        }
        if (gamepad_p1.JOY_B)
        {
            // 退出
            gamepad_p1.JOY_B = 0;
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
            return NULL;
        }
        if (gamepad_p1.JOY_A && gamepad_p1.JOY_SELECT == 0)
        {
            gamepad_p1.JOY_A = 0;
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS2);
            return mjpegFiles[cfg.menuCursor].file_name;
        }
        vTaskDelay(10);
    };
}

void mjpeg_loop()
{
    if (!cfg.sdOK)
    {
        draw_no_tf_card(0, 1);
        STAY_HERE
    }

    cfg.menuCursor = 0;
    cfg.menuPage = 0;
    clear_screen(false);
    if (!list_mjpeg_file())
    {
        cfg.currMode = MY_APP_NONE;
        return; // 退出
    }
    clear_screen(false);
    char *selectedFilename = NULL;
    char filePath[MAXFILENAME_LENGTH] = {0};
    // 判断自动游戏
    if (!cfg.mjpegEnterFlag)
    {
        // 仅当第一次进入该模式才执行
        if (load_auto_play(filePath, MY_APP_MJPEG))
        {
            // 判断读出来的文件名是否存在
            for (int i = 0; i < cfg.loadedMjpegFileNames; i++)
            {
                if (strcmp(mjpegFiles[i].file_name, filePath) == 0)
                {
                    // 存在
                    selectedFilename = filePath;
                    SHOW_MSG_SERIAL("Auto loading mjpeg: '")
                    SHOW_MSG_SERIAL(selectedFilename)
                    SHOW_MSG_SERIAL("'\n")
                    break;
                }
            }
            if (NULL == selectedFilename)
            {
                clear_auto_play(MY_APP_MJPEG);
                vTaskDelay(200);
            }
        }
        cfg.mjpegEnterFlag = 1;
    }
    while (1)
    {
        if (NULL == selectedFilename)
            selectedFilename = MJPEGMENU();
        if (NULL == selectedFilename)
        {
            vTaskDelay(100);
            cfg.currMode = MY_APP_NONE;
            return; // 退出
        }
        clear_screen(false);
        if (init_mjpeg_file(selectedFilename))
        {
            while (1)
            {
                mjpeg_draw_frame();
                input_refresh();
                if (gamepad_p1.JOY_B)
                {
                    // 退出播放
                    gamepad_p1.JOY_B = 0;
                    stopRead();
                    cfg.currMode = MY_APP_NONE;
                    vTaskDelay(UI_CONTROL_DEBOUNCE_MS2);
                    break;
                }
            }
        }
        selectedFilename = NULL;
    }
}
///////////////////////////////////////////////////
// MJPEG部分结束===================================
///////////////////////////////////////////////////

///////////////////////////////////////////////////
// AIDA64部分开始===================================
///////////////////////////////////////////////////
void refresh_pc_monitor()
{
    if (get_pc_data())
    {
        if (pc_data.update_ts != lastPcUpdateTs)
        {
            draw_pc_monitor_data();
            lastPcUpdateTs = pc_data.update_ts;
        }
    }
}

void start_pc_monitor_timer()
{
    stop_timer();
    timer_1 = xTimerCreate("pc_monitor", (TickType_t)PC_MONITOR_UPDATE_INTERVAL, pdTRUE, NULL, (TimerCallbackFunction_t)refresh_pc_monitor);
    xTimerStart(timer_1, 0);
}

void pc_monitor_loop()
{
    clear_screen(false);
    if (!pc_mon_init())
    {
        gfx->setFont(PIXEL_FONT_X1);
        gfx->setTextColor(RED);
        gfx->setCursor(10, 124);
        gfx->printf("WRONG PC HOST IP! %d.%d.%d.%d", save_cfg.ipAddrBytes[0], save_cfg.ipAddrBytes[1], save_cfg.ipAddrBytes[2], save_cfg.ipAddrBytes[3]);
        vTaskDelay(10000);
        cfg.currMode = MY_APP_SETTING;
        return;
    }
    clear_old_pc_monitor_data(); //清空缓存数据
    draw_pc_monitor_ui();
    start_pc_monitor_timer();
    while (1)
    {
        input_refresh();
        if (gamepad_p1.JOY_B)
        {
            gamepad_p1.JOY_B = 0;
            stop_timer();
            cfg.currMode = MY_APP_NONE;
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS2);
            break;
        }
        vTaskDelay(10);
    }
}
///////////////////////////////////////////////////
// AIDA64部分结束===================================
///////////////////////////////////////////////////

///////////////////////////////////////////////////
// 设置页面开始===================================
///////////////////////////////////////////////////
void setting_loop()
{
    cfg.menuCursor = 0;       // 一级菜单
    int8_t actMenu = -1;      // 选中的一级菜单
    int8_t subMenuCursor = 0; // 二级菜单
    clear_screen(false);
    draw_setting_ui(cfg.menuCursor);
    draw_controller_item(-1);
    uint8_t temp_ip[4];
    memcpy(temp_ip, save_cfg.ipAddrBytes, 4);
    draw_ip_addr(temp_ip, -1);

    draw_system_item(-1);
    uint32_t ts = 0;
    while (1)
    {
        input_refresh();
        if (actMenu == -1)
        {
            // 未进入子菜单
            if (gamepad_p1.JOY_UP)
            {
                if (cfg.menuCursor == 0)
                    cfg.menuCursor = 2;
                else
                    cfg.menuCursor--;
                input_clear();
                draw_setting_rect(cfg.menuCursor);
                vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
            }
            else if (gamepad_p1.JOY_DOWN)
            {
                if (++cfg.menuCursor > 2)
                {
                    cfg.menuCursor = 0;
                }
                input_clear();
                draw_setting_rect(cfg.menuCursor);
                vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
            }
            else if (gamepad_p1.JOY_A)
            {
                gamepad_p1.JOY_A = 0;
                actMenu = cfg.menuCursor;
                subMenuCursor = 0;
                if (cfg.menuCursor == 0)
                    draw_controller_item(subMenuCursor);
                else if (cfg.menuCursor == 1)
                    draw_ip_addr(temp_ip, subMenuCursor);
                else
                    draw_system_item(subMenuCursor);
                vTaskDelay(UI_CONTROL_DEBOUNCE_MS2);
            }
            else if (gamepad_p1.JOY_B)
            {
                gamepad_p1.JOY_B = 0;
                cfg.currMode = MY_APP_NONE;
                vTaskDelay(UI_CONTROL_DEBOUNCE_MS2);
                return;
            }
        }
        else if (actMenu == 0)
        {
            // controller菜单
            if (gamepad_p1.JOY_UP)
            {
                if (subMenuCursor == 0)
                    subMenuCursor = 2;
                else
                    subMenuCursor--;
                gamepad_p1.JOY_UP = 0;
                draw_controller_item(subMenuCursor);
                vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
            }
            else if (gamepad_p1.JOY_DOWN)
            {
                if (++subMenuCursor > 2)
                {
                    subMenuCursor = 0;
                }
                gamepad_p1.JOY_DOWN = 0;
                draw_controller_item(subMenuCursor);
                vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
            }
            else if (gamepad_p1.JOY_A)
            {
                cfg.controller = subMenuCursor + 1;
                save_cfg.controller = subMenuCursor + 1;
                save_config();
                vTaskDelay(1000);
                ESP.restart();
            }
            else if (gamepad_p1.JOY_B)
            {
                // 返回主菜单
                gamepad_p1.JOY_B = 0;
                actMenu = -1;
                draw_controller_item(-1);
                vTaskDelay(UI_CONTROL_DEBOUNCE_MS2);
            }
        }
        else if (actMenu == 1)
        {
            // host ip菜单
            ts = 0;
            if (gamepad_p1.JOY_LEFT)
            {
                if (subMenuCursor == 0)
                    subMenuCursor = 3;
                else
                    subMenuCursor--;
                gamepad_p1.JOY_LEFT = 0;
                draw_ip_addr(temp_ip, subMenuCursor);
                vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
            }
            else if (gamepad_p1.JOY_RIGHT)
            {
                if (++subMenuCursor > 3)
                {
                    subMenuCursor = 0;
                }
                gamepad_p1.JOY_RIGHT = 0;
                draw_ip_addr(temp_ip, subMenuCursor);
                vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
            }
            else if (gamepad_p1.JOY_UP)
            {
                ts = millis();
                vTaskDelay(200);
                input_refresh();
                while (gamepad_p1.JOY_UP)
                {
                    temp_ip[subMenuCursor]--;
                    draw_ip_addr_num(temp_ip[subMenuCursor], subMenuCursor);
                    vTaskDelay(20);
                    input_refresh();
                }
                temp_ip[subMenuCursor]--;
                draw_ip_addr_num(temp_ip[subMenuCursor], subMenuCursor);
            }
            else if (gamepad_p1.JOY_DOWN)
            {
                ts = millis();
                vTaskDelay(200);
                input_refresh();
                while (gamepad_p1.JOY_DOWN)
                {
                    temp_ip[subMenuCursor]++;
                    draw_ip_addr_num(temp_ip[subMenuCursor], subMenuCursor);
                    vTaskDelay(20);
                    input_refresh();
                }
                temp_ip[subMenuCursor]++;
                draw_ip_addr_num(temp_ip[subMenuCursor], subMenuCursor);
            }
            else if (gamepad_p1.JOY_A)
            {
                memcpy(save_cfg.ipAddrBytes, temp_ip, 4);
                save_config();
                gamepad_p1.JOY_A = 0;
                actMenu = -1;
                draw_ip_addr(temp_ip, -1);
                vTaskDelay(1000);
            }
            else if (gamepad_p1.JOY_B)
            {
                // 返回主菜单
                gamepad_p1.JOY_B = 0;
                actMenu = -1;
                draw_ip_addr(temp_ip, -1);
                vTaskDelay(UI_CONTROL_DEBOUNCE_MS2);
            }
        }
        else if (actMenu == 2)
        {
            if (gamepad_p1.JOY_LEFT)
            {
                if (subMenuCursor == 0)
                    subMenuCursor = 1;
                else
                    subMenuCursor--;
                gamepad_p1.JOY_LEFT = 0;
                draw_system_item(subMenuCursor);
                vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
            }
            else if (gamepad_p1.JOY_RIGHT)
            {
                if (++subMenuCursor > 1)
                {
                    subMenuCursor = 0;
                }
                gamepad_p1.JOY_RIGHT = 0;
                draw_system_item(subMenuCursor);
                vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
            }
            else if (gamepad_p1.JOY_A)
            {
                if (subMenuCursor == 0)
                {
                    // 清除wifi
                    save_cfg.wifiMode = 0;
                    save_config();
                }
                else if (subMenuCursor == 1)
                {
                    // 清除全部
                    clear_config();
                }
                gamepad_p1.JOY_A = 0;
                actMenu = -1;
                draw_system_item(-1);
                vTaskDelay(1000);
                ESP.restart();
            }
            else if (gamepad_p1.JOY_B)
            {
                // 返回主菜单
                gamepad_p1.JOY_B = 0;
                actMenu = -1;
                draw_system_item(-1);
                vTaskDelay(UI_CONTROL_DEBOUNCE_MS2);
            }
        }
        vTaskDelay(10);
    }
}
///////////////////////////////////////////////////
// 设置页面结束===================================
///////////////////////////////////////////////////

///////////////////////////////////////////////////
// 主页面开始===================================
///////////////////////////////////////////////////
void menu_loop()
{
    cfg.menuCursor = 0; // 只是索引
    draw_main_menu(cfg.menuCursor);
    while (1)
    {
#ifdef BAT_ADC_PIN
        if (millis() > last_batt_ts)
        {
            last_batt_ts = millis() + 5000;
            draw_batt_level(get_batt_level());
        }
#endif
        input_refresh();
        // START 将当前选中模式设为/取消开机自动模式
        // SELECT  静音/解除静音
        // A 进入选定功能
        // B 进入设置模式
        if (gamepad_p1.JOY_START)
        {
            SHOW_MSG_SERIAL("Set auto mode.")
            set_auto_mode(menus[cfg.menuCursor].APP_ID);
            input_clear();
            refresh_main_menu_text();
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS2);
        }
#if SOUND_ENABLED
        else if (gamepad_p1.JOY_B)
        {
            SHOW_MSG_SERIAL("Switch mute.")
            switch_mute();
            save_cfg.mute = cfg.mute;
            save_config();
            input_clear();
            draw_mute_icon();
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS2);
        }
#endif
        else if (gamepad_p1.JOY_LEFT)
        {
            if (cfg.menuCursor == 0)
                cfg.menuCursor = APP_MENU_NUM - 1;
            else
                cfg.menuCursor--;
            gamepad_p1.JOY_LEFT = 0;
            refresh_main_menu(cfg.menuCursor);
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
        }
        else if (gamepad_p1.JOY_RIGHT)
        {
            if (++cfg.menuCursor >= APP_MENU_NUM)
            {
                cfg.menuCursor = 0;
            }
            gamepad_p1.JOY_RIGHT = 0;
            refresh_main_menu(cfg.menuCursor);
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS1);
        }
        else if (gamepad_p1.JOY_SELECT)
        {
            gamepad_p1.JOY_SELECT = 0;
            cfg.currMode = MY_APP_SETTING;
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS2);
            return;
        }
        else if (gamepad_p1.JOY_A)
        {
            gamepad_p1.JOY_A = 0;
            cfg.currMode = menus[cfg.menuCursor].APP_ID;
            vTaskDelay(UI_CONTROL_DEBOUNCE_MS2);
            return;
        }
        vTaskDelay(10);
    }
}
///////////////////////////////////////////////////
// 主页面结束===================================
///////////////////////////////////////////////////
