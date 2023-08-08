#pragma once
#include <Arduino.h>

extern APP_MENU_OBJ menus[]; 

void clear_screen(bool w);
void reset_statusbar();

//MENU
void draw_main_menu(uint8_t curr);
void refresh_main_menu(uint8_t curr);
void refresh_main_menu_text();
void draw_lbw_text();
void draw_mute_icon();
void draw_batt_level(uint8_t lv);

//NES
void clear_nes_gamelist_area();
void draw_cart_logo(uint8_t cart_no);
void draw_game_list_ui(uint8_t files_num);
void draw_game_left_pane(uint8_t cart_no, uint8_t files_num);
void draw_nes_game_item(char *menu, uint8_t index, uint16_t page, bool isSelected,uint8_t files_num);
void draw_no_tf_card(uint8_t t, uint8_t m);
void draw_file_size(uint16_t file_size,uint8_t mode);

//AIDA64
void draw_pc_monitor_ui();
void draw_pc_monitor_data();
void clear_old_pc_monitor_data();

//MJPEG
void clear_mjpeg_filelist_area();
void draw_mjpeg_list_ui(uint8_t files_num);
void draw_mjpeg_file_item(char *menu, uint8_t index, uint16_t page, bool isSelected, uint8_t files_num);

//setting
void draw_setting_ui(uint8_t curr);
void draw_setting_rect(uint8_t curr);
void draw_controller_item(int8_t curr);
void draw_ip_addr(uint8_t ipaddr[4],int8_t pos);
void draw_ip_addr_num(uint8_t num, int8_t pos);
void draw_system_item(int8_t curr);
