#ifndef __IMGS_H
#define __IMGS_H

#include "config.h"

#include "tfcard.h"
#include "Cartridges.h"
#include "lbwmachine.h"
#include "setting_icon.h"
#if SCREEN_RES_HOR == 320
#include "nes_gamelist_bg.h"
#include "aida64_bg.h"
#include "mjpeg_bg.h"
#include "icon_mjpeg.h"
#include "icon_nes.h"
#include "icon_pcmon.h"
#elif SCREEN_RES_HOR == 240
#include "nes_gamelist_bg_240.h"
#include "mjpeg_bg_240.h"
#include "aida64_bg.h"
#include "icon_mjpeg_240.h"
#include "icon_nes_240.h"
#include "icon_pcmon_240.h"
#endif

#endif