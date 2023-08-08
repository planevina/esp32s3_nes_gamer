#include "joystick.h"
#include "config.h"
#include "../controller.h"

#include "usb_host_hid_bridge.h"
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>

static uint8_t joy_value = 0;

void config_desc_cb(const usb_config_desc_t *config_desc);
void device_info_cb(usb_device_info_t *dev_info);
void hid_report_descriptor_cb(usb_transfer_t *transfer);
void hid_report_cb(usb_transfer_t *transfer);

UsbHostHidBridge hidBridge;

void config_desc_cb(const usb_config_desc_t *config_desc)
{
    usb_print_config_descriptor(config_desc, NULL);
}

void device_info_cb(usb_device_info_t *dev_info)
{
    if (dev_info->str_desc_manufacturer)
        usb_print_string_descriptor(dev_info->str_desc_manufacturer);
    if (dev_info->str_desc_product)
        usb_print_string_descriptor(dev_info->str_desc_product);
    if (dev_info->str_desc_serial_num)
        usb_print_string_descriptor(dev_info->str_desc_serial_num);
}

void hid_report_descriptor_cb(usb_transfer_t *transfer)
{
    //>>>>> for HID Report Descriptor
    // Explanation: https://electronics.stackexchange.com/questions/68141/
    // USB Descriptor and Request Parser: https://eleccelerator.com/usbdescreqparser/#
    //<<<<<
    Serial.printf("\nstatus %d, actual number of bytes transferred %d\n", transfer->status, transfer->actual_num_bytes);
    for (int i = 0; i < transfer->actual_num_bytes; i++)
    {
        if (i == USB_SETUP_PACKET_SIZE)
        {
            Serial.printf("\n\n>>> Goto https://eleccelerator.com/usbdescreqparser/ \n");
            Serial.printf(">>> Copy & paste below HEX and parser as... USB HID Report Descriptor\n\n");
        }
        Serial.printf("%02X ", transfer->data_buffer[i]);
    }
    Serial.printf("\n\n");
    // Serial.printf("HID Report Descriptor\n");
    uint8_t *const data = (uint8_t *const)(transfer->data_buffer + USB_SETUP_PACKET_SIZE);
    size_t len = transfer->actual_num_bytes - USB_SETUP_PACKET_SIZE;
    // Serial.printf("> size: %ld bytes\n", len);
    bool isGamepad = false;
    bool isVenDef = false;
    if (len >= 5)
    {
        uint8_t gamepadUsagePage[] = {0x05, 0x01, 0x09, 0x05};
        uint8_t vdrDefUsagePage[] = {0x06, 0x00, 0xFF, 0x09, 0x01};
        isGamepad = memcmp(data, gamepadUsagePage, sizeof(gamepadUsagePage)) == 0;
        isVenDef = memcmp(data, vdrDefUsagePage, sizeof(vdrDefUsagePage)) == 0;
    }
    Serial.printf(">>> best guess: %s\n", isGamepad ? "HID Gamepad" : isVenDef ? "Vendor Defined"
                                                                               : "Unkown");
}

//
// check HID Report Descriptor for usage
//
void hid_report_cb(usb_transfer_t *transfer)
{
    uint8_t *data = (uint8_t *)(transfer->data_buffer);
    /*
     for (int i = 0; i < transfer->num_bytes; i++)
      {
          Serial.printf("%d,", data[i]);
      }
      Serial.println("");
        */
    joy_value = 0;
    if (transfer->num_bytes == 8)
    {
        // 只有返回数据长度8的时候才处理
        for (uint8_t i = 0; i < 4; i++)
        {
            // AB SELECT START
            if ((data[hid_key_map[i].pos] & hid_key_map[i].value) == hid_key_map[i].value)
            {
                joy_value |= (1 << i);
            }
        }
        for (uint8_t i = 4; i < 8; i++)
        {
            // 上下左右
            if (data[hid_key_map[i].pos] == hid_key_map[i].value)
            {
                joy_value |= (1 << i);
            }
        }
        for (uint8_t i = 8; i < 12; i++)
        {
            // X Y L R
            if ((data[hid_key_map[i].pos] & hid_key_map[i].value) == hid_key_map[i].value)
            {
                joy_value |= (1 << i - 8);
            }
        }
    }
}

uint8_t joystick_get_key_value()
{
    return joy_value;
}

void joystick_init()
{
    hidBridge.onConfigDescriptorReceived = config_desc_cb;
    hidBridge.onDeviceInfoReceived = device_info_cb;
    hidBridge.onHidReportDescriptorReceived = hid_report_descriptor_cb;
    hidBridge.onReportReceived = hid_report_cb;
    hidBridge.begin();
}
