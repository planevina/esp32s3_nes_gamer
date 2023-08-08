/****************************************************************************************************************************
  usb_host_hid_bridge.h
  For ESP32 S series boards

  ESP32 USB Host HID Brigde is a library for the ESP32/Arduino platform
  Built by Jeff Leung https://github.com/badjeff/ESP32-USB-Host-HID-Bridge
  Licensed under MIT license
  
  Version: 1.0.0

  Version Modified By   Date      Comments
  ------- -----------  ---------- -----------
  1.0.0   Jeff Leung   06/07/2022 Initial coding
  1.0.1   Jeff Leung   06/12/2022 Deprecapte semaphores
 *****************************************************************************************************************************/

#ifndef ESP32_USB_HID_HOST_BRIDGE_H /* include guards */
#define ESP32_USB_HID_HOST_BRIDGE_H

#include <Arduino.h>
#include <functional>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_intr_alloc.h"
#include "usb/usb_host.h"

// config
#define DAEMON_TASK_PRIORITY         1
#if !defined(DAEMON_TASK_COREID)
     #define DAEMON_TASK_COREID      0
#endif
#if !defined(DAEMON_TASK_LOOP_DELAY)
     #define DAEMON_TASK_LOOP_DELAY  100
#endif

#define CLASS_TASK_PRIORITY           1
#if !defined(CLASS_TASK_COREID)
     #define CLASS_TASK_COREID        0
#endif
#if !defined(CLASS_TASK_LOOP_DELAY)
     #define CLASS_TASK_LOOP_DELAY    5
#endif

#define CLIENT_NUM_EVENT_MSG    5  // usb_host_client_config_t.max_num_event_msg

class UsbHostHidBridge
{

public:
    UsbHostHidBridge();
    ~UsbHostHidBridge();
    void begin();
    void end();
    bool hostInstalled;
    void* driver_ptr;
    void (*onConfigDescriptorReceived)(const usb_config_desc_t *config_desc);
    void (*onDeviceInfoReceived)(usb_device_info_t *dev_info);
    void (*onHidReportDescriptorReceived)(usb_transfer_t *transfer);
    void (*onReportReceived)(usb_transfer_t *transfer);

protected:

};

#endif
