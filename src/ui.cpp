#include "main.h"
#include "display.h"
#include "network.h"
#include "display_util.h"
#include <Arduino_GFX_Library.h>
#include "ui.h"
#include "fonts/fontPressStart2P.h"
#include "fonts/lbwcn.h"
#include "pc_monitor/pc_monitor.h"
#include "mjpeg/mjpegplayer.h"
#include "image/imgs.h"

aida64_pc_data disp_data = {0};       // 当前显示的
static bool disp_wifi_status = false; // 当前显示的wifi状态

#if SCREEN_RES_HOR == 320
APP_MENU_OBJ menus[APP_MENU_NUM] = {
    {MY_APP_AIDA64, (const char *)"PC MONITOR", icon_pcmon, 44, 12},
    {MY_APP_NES, (const char *)"NES EMU", icon_nes, 31, 131},
    {MY_APP_MJPEG, (const char *)"MJPEG", icon_mjpeg, 47, 245}};
#elif SCREEN_RES_HOR == 240
APP_MENU_OBJ menus[APP_MENU_NUM] = {
    {MY_APP_AIDA64, (const char *)"PC MON", icon_pcmon_240, 29, 19},
    {MY_APP_NES, (const char *)"NES", icon_nes_240, 21, 110},
    {MY_APP_MJPEG, (const char *)"MJPEG", icon_mjpeg_240, 31, 183}};
#endif

void clear_screen(bool w)
{
    gfx->fillScreen(w ? WHITE : BLACK);
}

void reset_statusbar()
{
    gfx->setCursor(UI_STATUSBAR_CURSOR_X, UI_STATUSBAR_CURSOR_Y);
    gfx->fillRect(0, 224, SCREEN_RES_HOR, 16, 0);
    gfx->setFont();
    gfx->setTextColor(UI_TEXT_COLOR);
}

void draw_lbw_text()
{
    clear_screen(false);
    int x = SCREEN_RES_HOR / 2 - (24 * 6 / 2);
    int y = SCREEN_RES_VER / 2 + 12;
    gfx->setCursor(x, y);
    gfx->setFont(&lbwcn24px);
    gfx->print("012789");
#if SOUND_ENABLED
    vTaskDelay(cfg.mute ? 1000 : 2000);
#else
    vTaskDelay(1000);
#endif

    while (y > 40)
    {
        y -= 8;
        // 最后一次循环是36
        gfx->fillRect(x, y - 24, 144, 40, BLACK);
        gfx->setCursor(x, y);
        gfx->print("012789");
        vTaskDelay(20);
    }
}

void draw_key_tips(uint8_t mark)
{
#if SCREEN_RES_HOR == 320
    gfx->setFont(PIXEL_FONT_ICON);
    gfx->setTextColor(WHITE);
    gfx->setCursor(7, 236);
    gfx->print(FONT_ICON_SYMBOL_B);
    gfx->setCursor(129, 236);
    gfx->print(FONT_ICON_SYMBOL_A);
    gfx->setCursor(242, 236);
    gfx->print(FONT_ICON_SYMBOL_UDLR);

    gfx->setFont(PIXEL_FONT_X1);
    gfx->setCursor(32, 227);
    gfx->print("Back");
    gfx->setCursor(154, 227);

    if ((mark & 0x1) == 0x1)
        gfx->print("Play");
    else
        gfx->print("Confirm");

    gfx->setCursor(267, 227);
    gfx->print("Choose");

#elif SCREEN_RES_HOR == 240
    gfx->setFont(PIXEL_FONT_ICON);
    gfx->setTextColor(WHITE);
    gfx->setCursor(7, 236);
    gfx->print(FONT_ICON_SYMBOL_B);
    gfx->setCursor(78, 236);
    gfx->print(FONT_ICON_SYMBOL_A);
    gfx->setCursor(163, 236);
    gfx->print(FONT_ICON_SYMBOL_UDLR);

    gfx->setFont(PIXEL_FONT_X1);
    gfx->setCursor(32, 227);
    gfx->print("Back");
    gfx->setCursor(103, 227);
    if ((mark & 0x1) == 0x1)
        gfx->print("Play");
    else
        gfx->print("Confirm");
    gfx->setCursor(188, 227);
    gfx->print("Choose");
#endif
}

void draw_batt_level(uint8_t lv)
{
    gfx->fillRect(SCREEN_RES_HOR - 22, 10 , 16, 10, 0);
    gfx->setFont(PIXEL_FONT_X2);
    gfx->setTextColor(WHITE);
    gfx->setCursor(SCREEN_RES_HOR - 22, 20);

    switch (lv)
    {
    case 4:
        gfx->print(FONT_X2_SYMBOL_BAT_LEVEL_4);
        break;
    case 3:
        gfx->print(FONT_X2_SYMBOL_BAT_LEVEL_3);
        break;
    case 2:
        gfx->print(FONT_X2_SYMBOL_BAT_LEVEL_2);
        break;
    case 1:
        gfx->print(FONT_X2_SYMBOL_BAT_LEVEL_1);
        break;
    case 0:
        gfx->print(FONT_X2_SYMBOL_BAT_LEVEL_0);
        break;
    default:
        break;
    }
}

// 主菜单部分
void draw_mute_icon()
{
#if SOUND_ENABLED
#if SCREEN_RES_HOR == 320
    uint16_t xpos = (SCREEN_RES_HOR - 14) / 2;
    gfx->fillRect(xpos, 210, 20, 20, 0);

    if (cfg.mute)
    {
        gfx->setFont(PIXEL_FONT_X2);
        gfx->setTextColor(UI_MAIN_MENU_TEXT_COLOR);
        gfx->setCursor(xpos, 230);
        gfx->print(FONT_X2_SYMBOL_MUTE);
    }
#endif
#endif
}

void draw_main_menu(uint8_t curr)
{
    uint8_t icon_area_w = 80;
    uint8_t icon_area_h = 60;
#if SCREEN_RES_HOR == 240
    icon_area_w = 60;
    icon_area_h = 40;
#endif

    uint16_t xpos = 23;
    uint8_t ypos = (SCREEN_RES_VER - icon_area_h) / 2;
    clear_screen(false);
    gfx->setFont(&lbwcn24px);
    gfx->setCursor(SCREEN_RES_HOR / 2 - (24 * 6 / 2), 42);
    gfx->print("012789");
    gfx->setFont(PIXEL_FONT_X1);

    for (int i = 0; i < APP_MENU_NUM; i++)
    {
        // 文字
        gfx->setCursor(menus[i].text_x, 83);
        gfx->setTextColor(menus[i].APP_ID == save_cfg.favMode ? UI_MAIN_MENU_TEXT_FAV_COLOR : UI_MAIN_MENU_TEXT_COLOR);
        gfx->print(menus[i].menu_text);

        // 图标
        gfx->draw16bitRGBBitmap(xpos, (SCREEN_RES_VER - menus[i].icon_height) / 2, (uint16_t*)(menus[i].menu_icon), icon_area_w - 20, menus[i].icon_height);

        if (curr == i)
        {
            // 绘制选框
            gfx->drawRect(xpos - 10, ypos, icon_area_w, icon_area_h, YELLOW);
            gfx->drawFastHLine(xpos, ypos, icon_area_w - 20, BLACK);
            gfx->drawFastHLine(xpos, ypos + icon_area_h - 1, icon_area_w - 20, BLACK);
            gfx->drawFastVLine(xpos - 10, ypos + 10, icon_area_h - 20, BLACK);
            gfx->drawFastVLine(xpos + icon_area_w - 11, ypos + 10, icon_area_h - 20, BLACK);
        }
        xpos += (SCREEN_RES_HOR / 3);
    }

    // 底部的提示
#if SCREEN_RES_HOR == 320
    gfx->setFont(PIXEL_FONT_ICON);
    gfx->setTextColor(LIGHTGREY);
    gfx->setCursor(10, 206);
    gfx->print(FONT_ICON_SYMBOL_SELECT);

    gfx->setFont(PIXEL_FONT_X1);
    gfx->setCursor(70, 197);
    gfx->print("SETTING");

    gfx->setFont(PIXEL_FONT_ICON);
    gfx->setCursor(204, 206);
    gfx->print(FONT_ICON_SYMBOL_START);

    gfx->setFont(PIXEL_FONT_X1);
    gfx->setCursor(256, 197);
    gfx->print("AUTO");

#if SOUND_ENABLED
    gfx->setFont(PIXEL_FONT_ICON);
    gfx->setTextColor(LIGHTGREY);
    gfx->setCursor(10, 236);
    gfx->print(FONT_ICON_SYMBOL_B);

    gfx->setFont(PIXEL_FONT_X1);
    gfx->setCursor(38, 227);
    gfx->print("MUTE/UNMUTE");
#endif

    gfx->setFont(PIXEL_FONT_ICON);
    gfx->setTextColor(LIGHTGREY);
    gfx->setCursor(204, 236);
    gfx->print(FONT_ICON_SYMBOL_A);

    gfx->setFont(PIXEL_FONT_X1);
    gfx->setCursor(248, 227);
    gfx->print("ENTER");
#elif SCREEN_RES_HOR == 240
    gfx->setFont(PIXEL_FONT_ICON);
    gfx->setTextColor(LIGHTGREY);
    gfx->setCursor(10, 206);
    gfx->print(FONT_ICON_SYMBOL_SELECT);

    gfx->setFont(PIXEL_FONT_X1);
    gfx->setCursor(70, 197);
    gfx->print("SETTING");

    gfx->setFont(PIXEL_FONT_ICON);
    gfx->setCursor(134, 206);
    gfx->print(FONT_ICON_SYMBOL_START);

    gfx->setFont(PIXEL_FONT_X1);
    gfx->setCursor(186, 197);
    gfx->print("AUTO");

#if SOUND_ENABLED
    gfx->setFont(PIXEL_FONT_ICON);
    gfx->setTextColor(LIGHTGREY);
    gfx->setCursor(10, 236);
    gfx->print(FONT_ICON_SYMBOL_B);

    gfx->setFont(PIXEL_FONT_X1);
    gfx->setCursor(38, 227);
    gfx->print("MUTE/UNMUTE");
#endif

    gfx->setFont(PIXEL_FONT_ICON);
    gfx->setTextColor(LIGHTGREY);
    gfx->setCursor(144, 236);
    gfx->print(FONT_ICON_SYMBOL_A);

    gfx->setFont(PIXEL_FONT_X1);
    gfx->setCursor(178, 227);
    gfx->print("ENTER");
#endif

#if SOUND_ENABLED
    draw_mute_icon();
#endif
}

void refresh_main_menu_text()
{
    gfx->setFont(PIXEL_FONT_X1);
    uint16_t xpos = 23;
    for (int i = 0; i < APP_MENU_NUM; i++)
    {
        // 文字
        gfx->setCursor(menus[i].text_x, 83);
        gfx->setTextColor(menus[i].APP_ID == save_cfg.favMode ? UI_MAIN_MENU_TEXT_FAV_COLOR : UI_MAIN_MENU_TEXT_COLOR);
        gfx->print(menus[i].menu_text);
        xpos += (SCREEN_RES_HOR / 3);
    }
}

void refresh_main_menu(uint8_t curr)
{
    uint8_t icon_area_w = 80;
    uint8_t icon_area_h = 60;
#if SCREEN_RES_HOR == 240
    icon_area_w = 60;
    icon_area_h = 40;
#endif

    uint16_t xpos = 23;
    uint8_t ypos = (SCREEN_RES_VER - icon_area_h) / 2;
    for (int i = 0; i < APP_MENU_NUM; i++)
    {
        if (curr == i)
        {
            // 绘制选框
            gfx->drawRect(xpos - 10, ypos, icon_area_w, icon_area_h, YELLOW);
            gfx->drawFastHLine(xpos, ypos, icon_area_w - 20, BLACK);
            gfx->drawFastHLine(xpos, ypos + icon_area_h - 1, icon_area_w - 20, BLACK);
            gfx->drawFastVLine(xpos - 10, ypos + 10, icon_area_h - 20, BLACK);
            gfx->drawFastVLine(xpos + icon_area_w - 11, ypos + 10, icon_area_h - 20, BLACK);
        }
        else
        {
            // 删除选框
            gfx->drawRect(xpos - 10, ypos, icon_area_w, icon_area_h, BLACK);
        }
        xpos += (SCREEN_RES_HOR / 3);
    }
}

void draw_bg_part(const uint16_t *bg, int x, int y, int xoffset, int yoffset, int w, int h)
{
    for (int i = 0; i < h; i++)
    {
        gfx->draw16bitRGBBitmap(x, y + i, (uint16_t*)(bg) + SCREEN_RES_HOR * (yoffset + i) + xoffset, w, 1);
    }
}

// setting
void draw_setting_ui(uint8_t curr)
{
    gfx->setFont(PIXEL_FONT_X1);
    gfx->setTextColor(WHITE);
    gfx->setCursor((SCREEN_RES_HOR - 8 * 8) / 2, 24);
    gfx->print("SETTING");

    // 图标
    gfx->draw16bitRGBBitmap(4, 4, (uint16_t*)setting_icon, SETTING_ICON_WIDTH, SETTING_ICON_HEIGHT);

    // 分割条
    gfx->setCursor(0, 41);
#if SCREEN_RES_HOR == 320
    gfx->print("****************************************");
#elif SCREEN_RES_HOR == 240
    gfx->print("******************************");
#endif
    gfx->setCursor(0, 205);

#if SCREEN_RES_HOR == 320
    gfx->print("****************************************");
#elif SCREEN_RES_HOR == 240
    gfx->print("******************************");
#endif

    // 框
    uint8_t rect_w = 200;
#if SCREEN_RES_HOR == 240
    rect_w = 144;
#endif
    if (curr == 0)
    {
        gfx->drawRect(3, 45, rect_w, 72, UI_SETTING_ACTIVED_COLOR);
        gfx->drawRect(3, 120, rect_w, 36, UI_SETTING_COLOR);
        gfx->drawRect(3, 159, rect_w, 36, UI_SETTING_COLOR);
    }
    else if (curr == 1)
    {
        gfx->drawRect(3, 45, rect_w, 72, UI_SETTING_COLOR);
        gfx->drawRect(3, 120, rect_w, 36, UI_SETTING_ACTIVED_COLOR);
        gfx->drawRect(3, 159, rect_w, 36, UI_SETTING_COLOR);
    }
    else
    {
        gfx->drawRect(3, 45, rect_w, 72, UI_SETTING_COLOR);
        gfx->drawRect(3, 120, rect_w, 36, UI_SETTING_COLOR);
        gfx->drawRect(3, 159, rect_w, 36, UI_SETTING_ACTIVED_COLOR);
    }

    // 图标
#if SCREEN_RES_HOR == 320
    gfx->draw16bitRGBBitmap(230, 88, (uint16_t*)lbwmachine, 51, 64);
#elif SCREEN_RES_HOR == 240
    gfx->draw16bitRGBBitmap(173, 88, (uint16_t*)lbwmachine, 51, 64);
#endif

    gfx->setTextColor(LIGHTGREY);
    gfx->setCursor(10, 58);
    gfx->print("EXTRA CONTROLLER");

    gfx->setCursor(10, 133);
    gfx->print("AIDA64 HOST IP");

    gfx->setCursor(10, 172);
    gfx->print("CLEAR CONFIG");

    // 底部的提示
    draw_key_tips(0);
}

void draw_setting_rect(uint8_t curr)
{
    uint8_t rect_w = 200;
#if SCREEN_RES_HOR == 240
    rect_w = 144;
#endif
    if (curr == 0)
    {
        gfx->drawRect(3, 45, rect_w, 72, UI_SETTING_ACTIVED_COLOR);
        gfx->drawRect(3, 120, rect_w, 36, UI_SETTING_COLOR);
        gfx->drawRect(3, 159, rect_w, 36, UI_SETTING_COLOR);
    }
    else if (curr == 1)
    {
        gfx->drawRect(3, 45, rect_w, 72, UI_SETTING_COLOR);
        gfx->drawRect(3, 120, rect_w, 36, UI_SETTING_ACTIVED_COLOR);
        gfx->drawRect(3, 159, rect_w, 36, UI_SETTING_COLOR);
    }
    else
    {
        gfx->drawRect(3, 45, rect_w, 72, UI_SETTING_COLOR);
        gfx->drawRect(3, 120, rect_w, 36, UI_SETTING_COLOR);
        gfx->drawRect(3, 159, rect_w, 36, UI_SETTING_ACTIVED_COLOR);
    }
}

void draw_controller_item(int8_t curr)
{
    gfx->setFont(PIXEL_FONT_X1);
    uint8_t ypos = 76;
    gfx->setCursor(10, ypos);
    gfx->setTextColor(curr == 0 ? UI_CONTROLLER_LIST_ACTIVED_COLOR : UI_CONTROLLER_LIST_COLOR);
    gfx->print((cfg.controller == CONTROLLER_USB_HID_KBD) ? "@ KEYBOARD" : "$ KEYBOARD");

    ypos += 16;
    gfx->setCursor(10, ypos);
    gfx->setTextColor(curr == 1 ? UI_CONTROLLER_LIST_ACTIVED_COLOR : UI_CONTROLLER_LIST_COLOR);
    gfx->print((cfg.controller == CONTROLLER_USB_HID_JOYSTICK) ? "@ JOYSTICK" : "$ JOYSTICK");

    ypos += 16;
    gfx->setCursor(10, ypos);
    gfx->setTextColor(curr == 2 ? UI_CONTROLLER_LIST_ACTIVED_COLOR : UI_CONTROLLER_LIST_COLOR);
    gfx->print((cfg.controller == CONTROLLER_WECHAT_BLEPAD) ? "@ WECHAT PAD" : "$ WECHAT PAD");
}

void draw_ip_addr_num(uint8_t num, int8_t pos)
{
    gfx->setFont(PIXEL_FONT_X1);
    gfx->fillRect(10 + pos * 32, 143, 24, 8, 0);
    gfx->setCursor(10 + pos * 32, 151);
    gfx->setTextColor(UI_CONTROLLER_LIST_ACTIVED_COLOR);
    gfx->printf("%03d", num);
}

void draw_ip_addr(uint8_t ipaddr[4], int8_t pos)
{
    gfx->fillRect(10, 143, 128, 8, 0);
    gfx->setFont(PIXEL_FONT_X1);
    uint8_t ypos = 151;
    gfx->setCursor(10, ypos);
    for (int i = 0; i < 4; i++)
    {
        gfx->setTextColor((pos == i) ? UI_CONTROLLER_LIST_ACTIVED_COLOR : UI_CONTROLLER_LIST_COLOR);
        gfx->printf("%03d", ipaddr[i]);
        if (i < 3)
            gfx->print(".");
    }
}

void draw_system_item(int8_t curr)
{
    gfx->setFont(PIXEL_FONT_X1);
    gfx->setCursor(10, 190);
    gfx->setTextColor(curr == 0 ? UI_CONTROLLER_LIST_ACTIVED_COLOR : UI_CONTROLLER_LIST_COLOR);
    gfx->print("WIFI");

    gfx->setCursor(70, 190);
    gfx->setTextColor(curr == 1 ? UI_CONTROLLER_LIST_ACTIVED_COLOR : UI_CONTROLLER_LIST_COLOR);
    gfx->print("ALL");
}

// MJPEG
void clear_mjpeg_filelist_area()
{
#if SCREEN_RES_HOR == 320
    draw_bg_part(mjpeg_bg, 90, 60, 90, 60, 166, 120);
#endif
#if SCREEN_RES_HOR == 240
    draw_bg_part(mjpeg_bg_240, 90, 60, 90, 60, 86, 120);
#endif
}

// 绘制列表总体背景
void draw_mjpeg_list_ui(uint8_t files_num)
{
    clear_screen(false);
#if SCREEN_RES_HOR == 320
    gfx->draw16bitRGBBitmap(0, 0, (uint16_t*)mjpeg_bg, 320, 240);
#endif
#if SCREEN_RES_HOR == 240
    gfx->draw16bitRGBBitmap(0, 0, mjpeg_bg_240, 240, 240);
#endif

    gfx->setFont(PIXEL_FONT_X1);
    gfx->setCursor((SCREEN_RES_HOR - 12 * 8) / 2, 24);
    gfx->setTextColor(WHITE);
    gfx->print("MJPEG PLAYER");

    // 底部的提示
    draw_key_tips(1);

    uint16_t char_w = get_num_width(files_num) * 8;
    gfx->setCursor(36 - char_w / 2, 80);
    gfx->setTextColor(WHITE);
    gfx->print(files_num);
}

void draw_mjpeg_file_item(char *menu, uint8_t index, uint16_t page, bool isSelected, uint8_t files_num)
{
    gfx->setFont(PIXEL_FONT_X1);
    gfx->setTextColor(isSelected ? UI_MJPEG_LIST_SELECTED_COLOR : UI_MJPEG_LIST_COLOR);
    gfx->setCursor(90, 71 + 12 * index);
    if (files_num > 99)
        gfx->printf("%03d.%s", page * FILES_PER_PAGE + index + 1, menu);
    else if (files_num > 9)
        gfx->printf("%02d.%s", page * FILES_PER_PAGE + index + 1, menu);
    else
        gfx->printf("%d.%s", page * FILES_PER_PAGE + index + 1, menu);
}

// NES部分
// 清除NES游戏列表区域
void clear_nes_gamelist_area()
{
#if SCREEN_RES_HOR == 320
    gfx->draw16bitRGBBitmap(0, 46, (uint16_t*)(nes_gamelist_bg) + SCREEN_RES_HOR * 46, SCREEN_RES_HOR, 148);
#endif
#if SCREEN_RES_HOR == 240
    gfx->draw16bitRGBBitmap(0, 46, nes_gamelist_bg_240 + SCREEN_RES_HOR * 46, SCREEN_RES_HOR, 148);
#endif
}

// 绘制卡带图
void draw_cart_logo(uint8_t cart_no)
{
    if (cart_no > 4)
        cart_no = 0;
    switch (cart_no)
    {
    case 0:
        gfx->draw16bitRGBBitmap(10, 62, (uint16_t*)Cartridges0, 68, 40);
        break;
    case 1:
        gfx->draw16bitRGBBitmap(10, 62, (uint16_t*)Cartridges1, 68, 40);
        break;
    case 2:
        gfx->draw16bitRGBBitmap(10, 62, (uint16_t*)Cartridges2, 68, 40);
        break;
    case 3:
        gfx->draw16bitRGBBitmap(10, 62, (uint16_t*)Cartridges3, 68, 40);
        break;
    case 4:
        gfx->draw16bitRGBBitmap(10, 62, (uint16_t*)Cartridges4, 68, 40);
        break;
    }
}

// 显示文件大小
void draw_file_size(uint16_t file_size, uint8_t mode)
{
    gfx->setFont(PIXEL_FONT_X1);
    gfx->setTextColor(UI_TEXT_COLOR);
    if (mode == MY_APP_NES)
    {
#if SCREEN_RES_HOR == 320
        draw_bg_part(nes_gamelist_bg, 10, 183, 10, 183, 40, 8);
        gfx->setCursor(10, 191);
#elif SCREEN_RES_HOR == 240
        draw_bg_part(nes_gamelist_bg_240, 10, 183, 10, 183, 40, 8);
        gfx->setCursor(10, 191);
#endif
        gfx->print(file_size);
    }
    else if (mode == MY_APP_MJPEG)
    {
#if SCREEN_RES_HOR == 320
        draw_bg_part(mjpeg_bg, 276, 158, 276, 158, 40, 8);
        gfx->setCursor(276, 166);
#elif SCREEN_RES_HOR == 240
        draw_bg_part(mjpeg_bg_240, 10, 104, 10, 104, 40, 8);
        gfx->setCursor(10, 112);
#endif
        gfx->print(file_size);
    }
}

// 绘制游戏列表
void draw_nes_game_item(char *menu, uint8_t index, uint16_t page, bool isSelected, uint8_t files_num)
{
    gfx->setFont(PIXEL_FONT_X1);
    gfx->setTextColor(isSelected ? UI_GAME_LIST_SELECTED_COLOR : UI_GAME_LIST_COLOR);
    gfx->setCursor(93, 60 + 14 * index);
    if (files_num > 99)
        gfx->printf("%03d.%s", page * FILES_PER_PAGE + index + 1, menu);
    else if (files_num > 9)
        gfx->printf("%02d.%s", page * FILES_PER_PAGE + index + 1, menu);
    else
        gfx->printf("%d.%s", page * FILES_PER_PAGE + index + 1, menu);
}

// 绘制游戏列表左部分
void draw_game_left_pane(uint8_t cart_no, uint8_t files_num)
{
    draw_cart_logo(cart_no);
    gfx->setTextColor(UI_TEXT_COLOR);
    gfx->setCursor(10, 112);
    gfx->printf("%d IN 1", files_num);
}

// 绘制游戏列表总体背景
void draw_game_list_ui(uint8_t files_num)
{
    clear_screen(false);
#if SCREEN_RES_HOR == 320
    gfx->draw16bitRGBBitmap(0, 0, (uint16_t*)nes_gamelist_bg, 320, 240);
#elif SCREEN_RES_HOR == 240
    gfx->draw16bitRGBBitmap(0, 0, nes_gamelist_bg_240, 240, 240);
#endif

    gfx->setFont(PIXEL_FONT_X1);
    gfx->setCursor((SCREEN_RES_HOR - 21 * 8) / 2, 24);
    gfx->setTextColor(WHITE);
    gfx->print("LAO BA WANG NES GAMER");

    // 底部的提示
    draw_key_tips(1);
}

// 绘制无TF卡提示
void draw_no_tf_card(uint8_t t, uint8_t m)
{
    clear_screen(false);
    uint16_t xpos = 60;
#if SCREEN_RES_HOR == 240
    xpos = 10;
#endif
    gfx->draw16bitRGBBitmap(xpos, 92, (uint16_t*)tfcard, 40, 55);
    gfx->setFont(PIXEL_FONT_X1);
    gfx->setTextColor(RGB565_ORANGE);
    uint16_t char_len = 0;
    if (m == 1)
    {
        char_len = 10 * 8;
        gfx->setCursor((SCREEN_RES_HOR - char_len) / 2, 40);
        gfx->print("No TF card");
    }
    else if (m == 2)
    {
        char_len = 9 * 8;
        gfx->setCursor((SCREEN_RES_HOR - char_len) / 2, 40);
        gfx->print("No Folder");
    }

    gfx->setTextColor(UI_TEXT_COLOR);
    gfx->setCursor(xpos + 50, 78);
    gfx->print("Format : FAT32");
    gfx->setCursor(xpos + 50, 108);
    if (t == 1)
        gfx->print("Folder : /NES");
    else
        gfx->print("Folder : /mjpeg");
    gfx->setCursor(xpos + 50, 138);
    if (t == 1)
        gfx->print("Ext : .nes");
    else
        gfx->print("Ext : .mjpeg");
    gfx->setCursor(xpos + 50, 168);
    gfx->print("Max Files : 255");
}
// NES部分结束

// AIDA64部分

// 绘制AIDA64整体UI
void draw_pc_monitor_ui()
{
    clear_screen(false);
#if SCREEN_RES_HOR == 320
    gfx->draw16bitRGBBitmap(0, 0, (uint16_t*)aida64_bg, 320, 240);
#endif
    gfx->setFont(PIXEL_FONT_X1);
    gfx->setCursor((SCREEN_RES_HOR - 17 * 8) / 2, 24);
    gfx->setTextColor(UI_AIDA64_COLOR);
    gfx->print("AIDA64 PC MONITOR");

    gfx->setCursor(SCREEN_RES_HOR - 128, 227);
    gfx->print(pc_host.toString());

    gfx->setFont(PIXEL_FONT_X2);
    gfx->setCursor(SCREEN_RES_HOR - 24, 24);
    gfx->setTextColor(UI_AIDA64_COLOR);

    disp_wifi_status = get_wifi_status();
    gfx->print(disp_wifi_status ? FONT_X2_SYMBOL_WIFI_CONNECTED : FONT_X2_SYMBOL_WIFI_DISCONNECTED);
}

//清空缓存数据
void clear_old_pc_monitor_data()
{
    disp_data = {0};
}

// 刷新AIDA64显示数据
void draw_pc_monitor_data()
{
    uint16_t xpos = 90;
    uint16_t ypos = 72;
    uint16_t char_width = 0;
    uint16_t char_max_width = 0;
    gfx->setTextColor(UI_AIDA64_COLOR);
    gfx->setFont(PIXEL_FONT_X2);
    char_max_width = 16 * 3; // 最多3个字符
    if (pc_data.gpu_usage != disp_data.gpu_usage)
    {
        char_width = get_num_width(pc_data.gpu_usage) * 16;
        draw_bg_part(aida64_bg, xpos - char_max_width, ypos - 16, xpos - char_max_width, ypos - 16, char_max_width, 16);
        gfx->setCursor(xpos - char_width, ypos);
        gfx->print(pc_data.gpu_usage);
        disp_data.gpu_usage = pc_data.gpu_usage;
    }

    xpos += 104;
    if (pc_data.cpu_usage != disp_data.cpu_usage)
    {
        char_width = get_num_width(pc_data.cpu_usage) * 16;
        draw_bg_part(aida64_bg, xpos - char_max_width, ypos - 16, xpos - char_max_width, ypos - 16, char_max_width, 16);
        gfx->setCursor(xpos - char_width, ypos);
        gfx->print(pc_data.cpu_usage);
        disp_data.cpu_usage = pc_data.cpu_usage;
    }

    xpos += 104;
    if (pc_data.ram_usage != disp_data.ram_usage)
    {
        char_width = get_num_width(pc_data.ram_usage) * 16;
        draw_bg_part(aida64_bg, xpos - char_max_width, ypos - 16, xpos - char_max_width, ypos - 16, char_max_width, 16);
        gfx->setCursor(xpos - char_width, ypos);
        gfx->print(pc_data.ram_usage);
        disp_data.ram_usage = pc_data.ram_usage;
    }

    xpos = 74;
    ypos = 102;
    char_max_width = 16 * 4; // 最多4个字符
    if (pc_data.gpu_freq != disp_data.gpu_freq)
    {
        char_width = get_num_width(pc_data.gpu_freq) * 16;
        draw_bg_part(aida64_bg, xpos - char_max_width, ypos - 16, xpos - char_max_width, ypos - 16, char_max_width, 16);
        gfx->setCursor(xpos - char_width, ypos);
        gfx->print(pc_data.gpu_freq);
        disp_data.gpu_freq = pc_data.gpu_freq;
    }
    ypos += 24;
    if (pc_data.gpu_temp != disp_data.gpu_temp)
    {
        char_width = get_num_width(pc_data.gpu_temp) * 16;
        draw_bg_part(aida64_bg, xpos - char_max_width, ypos - 16, xpos - char_max_width, ypos - 16, char_max_width, 16);
        gfx->setCursor(xpos - char_width, ypos);
        gfx->print(pc_data.gpu_temp);
        disp_data.gpu_temp = pc_data.gpu_temp;
    }
    ypos += 24;
    if (pc_data.gpu_fan != disp_data.gpu_fan)
    {
        char_width = get_num_width(pc_data.gpu_fan) * 16;
        draw_bg_part(aida64_bg, xpos - char_max_width, ypos - 16, xpos - char_max_width, ypos - 16, char_max_width, 16);
        gfx->setCursor(xpos - char_width, ypos);
        gfx->print(pc_data.gpu_fan);
        disp_data.gpu_fan = pc_data.gpu_fan;
    }

    xpos += 104;
    ypos = 102;
    if (pc_data.cpu_freq != disp_data.cpu_freq)
    {
        char_width = get_num_width(pc_data.cpu_freq) * 16;
        draw_bg_part(aida64_bg, xpos - char_max_width, ypos - 16, xpos - char_max_width, ypos - 16, char_max_width, 16);
        gfx->setCursor(xpos - char_width, ypos);
        gfx->print(pc_data.cpu_freq);
        disp_data.cpu_freq = pc_data.cpu_freq;
    }
    ypos += 24;
    if (pc_data.cpu_temp != disp_data.cpu_temp)
    {
        char_width = get_num_width(pc_data.cpu_temp) * 16;
        draw_bg_part(aida64_bg, xpos - char_max_width, ypos - 16, xpos - char_max_width, ypos - 16, char_max_width, 16);
        gfx->setCursor(xpos - char_width, ypos);
        gfx->print(pc_data.cpu_temp);
        disp_data.cpu_temp = pc_data.cpu_temp;
    }
    ypos += 24;
    if (pc_data.cpu_fan != disp_data.cpu_fan)
    {
        char_width = get_num_width(pc_data.cpu_fan) * 16;
        draw_bg_part(aida64_bg, xpos - char_max_width, ypos - 16, xpos - char_max_width, ypos - 16, char_max_width, 16);
        gfx->setCursor(xpos - char_width, ypos);
        gfx->print(pc_data.cpu_fan);
        disp_data.cpu_fan = pc_data.cpu_fan;
    }

    xpos += 120;
    ypos = 102;
    char_max_width = 16 * 5;
    if (pc_data.ram_avl != disp_data.ram_avl)
    {
        char_width = get_num_width(pc_data.ram_avl) * 16;
        draw_bg_part(aida64_bg, xpos - char_max_width, ypos - 16, xpos - char_max_width, ypos - 16, char_max_width, 16);
        gfx->setCursor(xpos - char_width, ypos);
        gfx->print(pc_data.ram_avl);
        disp_data.ram_avl = pc_data.ram_avl;
    }

    xpos = 273;
    ypos = 145;
    char_max_width = 16 * 2;
    if (pc_data.mb_temp != disp_data.mb_temp)
    {
        char_width = get_num_width(pc_data.mb_temp) * 16;
        draw_bg_part(aida64_bg, xpos - char_max_width, ypos - 16, xpos - char_max_width, ypos - 16, char_max_width, 16);
        gfx->setCursor(xpos - char_width, ypos);
        gfx->print(pc_data.mb_temp);
        disp_data.mb_temp = pc_data.mb_temp;
    }

    gfx->setFont(PIXEL_FONT_X1);
    xpos = 198;
    ypos = 174;
    char_max_width = 8 * 2;
    if (pc_data.disk1_temp != disp_data.disk1_temp)
    {
        char_width = get_num_width(pc_data.disk1_temp) * 8;
        draw_bg_part(aida64_bg, xpos - char_max_width, ypos - 8, xpos - char_max_width, ypos - 8, char_max_width, 8);
        gfx->setCursor(xpos - char_width, ypos);
        gfx->print(pc_data.disk1_temp);
        disp_data.disk1_temp = pc_data.disk1_temp;
    }
    ypos = 188;
    if (pc_data.disk2_temp != disp_data.disk2_temp)
    {
        char_width = get_num_width(pc_data.disk2_temp) * 8;
        draw_bg_part(aida64_bg, xpos - char_max_width, ypos - 8, xpos - char_max_width, ypos - 8, char_max_width, 8);
        gfx->setCursor(xpos - char_width, ypos);
        gfx->print(pc_data.disk2_temp);
        disp_data.disk2_temp = pc_data.disk2_temp;
    }

    xpos = 235;
    ypos = 174;
    if (pc_data.net_upload_speed != disp_data.net_upload_speed)
    {
        char_width = 80;
        draw_bg_part(aida64_bg, xpos, ypos - 8, xpos, ypos - 8, char_width, 8);
        gfx->setCursor(xpos, ypos);
        if (pc_data.net_upload_speed < 10240)
        {
            // KB
            gfx->printf("%d.%dK", pc_data.net_upload_speed / 10, pc_data.net_upload_speed % 10);
        }
        else if (pc_data.net_upload_speed < 10485760)
        {
            // MB
            gfx->printf("%d.%dM", pc_data.net_upload_speed / 10240, (pc_data.net_upload_speed / 1024) % 10);
        }
        else
        {
            // GB
            gfx->printf("%d.%dG", pc_data.net_upload_speed / 10485760, (pc_data.net_upload_speed / 1048576) % 10);
        }

        disp_data.net_upload_speed = pc_data.net_upload_speed;
    }
    ypos = 188;

    if (pc_data.net_download_speed != disp_data.net_download_speed)
    {
        char_width = 80;
        draw_bg_part(aida64_bg, xpos, ypos - 8, xpos, ypos - 8, char_width, 8);
        gfx->setCursor(xpos, ypos);
        if (pc_data.net_download_speed < 10240)
        {
            // KB
            gfx->printf("%d.%dK", pc_data.net_download_speed / 10, pc_data.net_download_speed % 10);
        }
        else if (pc_data.net_download_speed < 10485760)
        {
            // MB
            gfx->printf("%d.%dM", pc_data.net_download_speed / 10240, (pc_data.net_download_speed / 1024) % 10);
        }
        else
        {
            // GB
            gfx->printf("%d.%dG", pc_data.net_download_speed / 10485760, (pc_data.net_download_speed / 1048576) % 10);
        }

        disp_data.net_download_speed = pc_data.net_download_speed;
    }

    xpos = 80;
    ypos = 173;
    uint8_t l;
    if (pc_data.disk1_usage != disp_data.disk1_usage)
    {
        l = pc_data.disk1_usage / 10;
        char_width = 80;
        draw_bg_part(aida64_bg, xpos, ypos - 8, xpos, ypos - 8, char_width, 8);
        gfx->setCursor(xpos, ypos);
        for (int i = 0; i < 10; i++)
        {
            if (i < l)
                gfx->setTextColor(UI_AIDA64_COLOR);
            else
                gfx->setTextColor(RGB565(220, 220, 220));
            gfx->print("~");
        }
        disp_data.disk1_usage = pc_data.disk1_usage;
    }

    ypos = 187;
    if (pc_data.disk2_usage != disp_data.disk2_usage)
    {
        l = pc_data.disk2_usage / 10;
        char_width = 80;
        draw_bg_part(aida64_bg, xpos, ypos - 8, xpos, ypos - 8, char_width, 8);
        gfx->setCursor(xpos, ypos);
        for (int i = 0; i < 10; i++)
        {
            if (i < l)
                gfx->setTextColor(UI_AIDA64_COLOR);
            else
                gfx->setTextColor(RGB565(220, 220, 220));
            gfx->print("~");
        }
        disp_data.disk2_usage = pc_data.disk2_usage;
    }
    if (disp_wifi_status != get_wifi_status())
    {
        disp_wifi_status = get_wifi_status();
        draw_bg_part(aida64_bg, SCREEN_RES_HOR - 24, 6, SCREEN_RES_HOR - 24, 10, 14, 18);
        gfx->setFont(PIXEL_FONT_X2);
        gfx->setCursor(SCREEN_RES_HOR - 24, 24);
        gfx->setTextColor(UI_AIDA64_COLOR);
        gfx->print(disp_wifi_status ? FONT_X2_SYMBOL_WIFI_CONNECTED : FONT_X2_SYMBOL_WIFI_DISCONNECTED);
    }
}
