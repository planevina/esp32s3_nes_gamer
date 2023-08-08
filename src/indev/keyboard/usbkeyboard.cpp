#include "usbkeyboard.h"
#include "config.h"
#include "../controller.h"


#include <elapsedMillis.h>
#include <usb/usb_host.h>
#include "show_desc.hpp"
#include "usbhhelp.hpp"
#include "esp_log.h"

// PLAYER1
#define KB_CODE_W 0x1A
#define KB_CODE_S 0x16
#define KB_CODE_A 0x04
#define KB_CODE_D 0x07
#define KB_CODE_U 0x18
#define KB_CODE_J 0x0D
#define KB_CODE_K 0x0E
#define KB_CODE_I 0x0C
#define KB_CODE_F1 0x3A
#define KB_CODE_F2 0x3B

// PLAYER2
#define KB_CODE_UP 0x52
#define KB_CODE_DOWN 0x51
#define KB_CODE_LEFT 0x50
#define KB_CODE_RIGHT 0x4F
#define KB_CODE_NUM2 0x5A
#define KB_CODE_NUM3 0x5B
#define KB_CODE_NUM5 0x5D
#define KB_CODE_NUM6 0x5E
#define KB_CODE_NUM8 0x60
#define KB_CODE_NUM9 0x61

static uint8_t kb_value = 0;
static uint8_t kb_p2_value = 0;

bool isKeyboard = false;
bool isKeyboardReady = false;
uint8_t KeyboardInterval;
bool isKeyboardPolling = false;
elapsedMillis KeyboardTimer;

const size_t KEYBOARD_IN_BUFFER_SIZE = 8;
usb_transfer_t *KeyboardIn = NULL;

TaskHandle_t TASK_HOST_KEYBOARD_HANDLE;

void keyboard_transfer_cb(usb_transfer_t *transfer)
{
    if (Device_Handle == transfer->device_handle)
    {
        isKeyboardPolling = false;
        if (transfer->status == 0)
        {
            kb_value = 0;
            kb_p2_value = 0;
            if (transfer->actual_num_bytes == 8)
            {
                uint8_t *const p = transfer->data_buffer;

                for (int i = 2; i < 8; i++)
                {
                    switch (p[i])
                    {
                    case KB_CODE_W:
                        kb_value |= GAMEPAD_KEY_UP;
                        break;
                    case KB_CODE_S:
                        kb_value |= GAMEPAD_KEY_DOWN;
                        break;
                    case KB_CODE_A:
                        kb_value |= GAMEPAD_KEY_LEFT;
                        break;
                    case KB_CODE_D:
                        kb_value |= GAMEPAD_KEY_RIGHT;
                        break;
                    case KB_CODE_J:
                    case KB_CODE_U:
                        kb_value |= GAMEPAD_KEY_B;
                        break;
                    case KB_CODE_K:
                    case KB_CODE_I:
                        kb_value |= GAMEPAD_KEY_A;
                        break;
                    case KB_CODE_F1:
                        kb_value |= GAMEPAD_KEY_SELECT;
                        break;
                    case KB_CODE_F2:
                        kb_value |= GAMEPAD_KEY_START;
                        break;

                    case KB_CODE_UP:
                        kb_p2_value |= GAMEPAD_KEY_UP;
                        break;
                    case KB_CODE_DOWN:
                        kb_p2_value |= GAMEPAD_KEY_DOWN;
                        break;
                    case KB_CODE_LEFT:
                        kb_p2_value |= GAMEPAD_KEY_LEFT;
                        break;
                    case KB_CODE_RIGHT:
                        kb_p2_value |= GAMEPAD_KEY_RIGHT;
                        break;

                    case KB_CODE_NUM2:
                    case KB_CODE_NUM5:
                        kb_p2_value |= GAMEPAD_KEY_B;
                        break;
                    case KB_CODE_NUM3:
                    case KB_CODE_NUM6:
                        kb_p2_value |= GAMEPAD_KEY_A;
                        break;

                    case KB_CODE_NUM8:
                        kb_p2_value |= GAMEPAD_KEY_SELECT;
                        break;
                    case KB_CODE_NUM9:
                        kb_p2_value |= GAMEPAD_KEY_START;
                        break;
                    default:
                        break;
                    }
                }
            }
            else
            {
                ESP_LOGI("", "Keyboard boot hid transfer too short or long");
            }
        }
        else
        {
            ESP_LOGI("", "transfer->status %d", transfer->status);
        }
    }
}

void check_interface_desc_boot_keyboard(const void *p)
{
    const usb_intf_desc_t *intf = (const usb_intf_desc_t *)p;

    if ((intf->bInterfaceClass == USB_CLASS_HID) &&
        (intf->bInterfaceSubClass == 1) &&
        (intf->bInterfaceProtocol == 1))
    {
        isKeyboard = true;
        ESP_LOGI("", "Claiming a boot keyboard!");
        esp_err_t err = usb_host_interface_claim(Client_Handle, Device_Handle,
                                                 intf->bInterfaceNumber, intf->bAlternateSetting);
        if (err != ESP_OK)
            ESP_LOGI("", "usb_host_interface_claim failed: %x", err);
    }
}

void prepare_endpoint(const void *p)
{
    const usb_ep_desc_t *endpoint = (const usb_ep_desc_t *)p;
    esp_err_t err;

    if ((endpoint->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) != USB_BM_ATTRIBUTES_XFER_INT)
    {
        ESP_LOGI("", "Not interrupt endpoint: 0x%02x", endpoint->bmAttributes);
        return;
    }
    if (endpoint->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK)
    {
        err = usb_host_transfer_alloc(KEYBOARD_IN_BUFFER_SIZE, 0, &KeyboardIn);
        if (err != ESP_OK)
        {
            KeyboardIn = NULL;
            ESP_LOGI("", "usb_host_transfer_alloc In fail: %x", err);
            return;
        }
        KeyboardIn->device_handle = Device_Handle;
        KeyboardIn->bEndpointAddress = endpoint->bEndpointAddress;
        KeyboardIn->callback = keyboard_transfer_cb;
        KeyboardIn->context = NULL;
        isKeyboardReady = true;
        KeyboardInterval = endpoint->bInterval;
        ESP_LOGI("", "USB boot keyboard ready");
    }
    else
    {
        ESP_LOGI("", "Ignoring interrupt Out endpoint");
    }
}

void show_config_desc_full(const usb_config_desc_t *config_desc)
{
    // Full decode of config desc.
    const uint8_t *p = &config_desc->val[0];
    static uint8_t USB_Class = 0;
    uint8_t bLength;
    for (int i = 0; i < config_desc->wTotalLength; i += bLength, p += bLength)
    {
        bLength = *p;
        if ((i + bLength) <= config_desc->wTotalLength)
        {
            const uint8_t bDescriptorType = *(p + 1);
            switch (bDescriptorType)
            {
            case USB_B_DESCRIPTOR_TYPE_DEVICE:
                ESP_LOGI("", "USB Device Descriptor should not appear in config");
                break;
            case USB_B_DESCRIPTOR_TYPE_CONFIGURATION:
                show_config_desc(p);
                break;
            case USB_B_DESCRIPTOR_TYPE_STRING:
                ESP_LOGI("", "USB string desc TBD");
                break;
            case USB_B_DESCRIPTOR_TYPE_INTERFACE:
                USB_Class = show_interface_desc(p);
                check_interface_desc_boot_keyboard(p);
                break;
            case USB_B_DESCRIPTOR_TYPE_ENDPOINT:
                show_endpoint_desc(p);
                if (isKeyboard && KeyboardIn == NULL)
                    prepare_endpoint(p);
                break;
            case USB_B_DESCRIPTOR_TYPE_DEVICE_QUALIFIER:
                // Should not be config config?
                ESP_LOGI("", "USB device qual desc TBD");
                break;
            case USB_B_DESCRIPTOR_TYPE_OTHER_SPEED_CONFIGURATION:
                // Should not be config config?
                ESP_LOGI("", "USB Other Speed TBD");
                break;
            case USB_B_DESCRIPTOR_TYPE_INTERFACE_POWER:
                // Should not be config config?
                ESP_LOGI("", "USB Interface Power TBD");
                break;
            case 0x21:
                if (USB_Class == USB_CLASS_HID)
                {
                    show_hid_desc(p);
                }
                break;
            default:
                ESP_LOGI("", "Unknown USB Descriptor Type: 0x%x", bDescriptorType);
                break;
            }
        }
        else
        {
            ESP_LOGI("", "USB Descriptor invalid");
            return;
        }
    }
}

uint8_t keyboard_get_key_value()
{
    return kb_value;
}

uint8_t keyboard_p2_get_key_value()
{
    return kb_p2_value;
}

void keyboard_task(void *pvParameters)
{
    while (1)
    {
        usbh_task();

        if (isKeyboardReady && !isKeyboardPolling && (KeyboardTimer > KeyboardInterval))
        {
            KeyboardIn->num_bytes = 8;
            esp_err_t err = usb_host_transfer_submit(KeyboardIn);
            if (err != ESP_OK)
            {
                ESP_LOGI("", "usb_host_transfer_submit In fail: %x", err);
            }
            isKeyboardPolling = true;
            KeyboardTimer = 0;
        }
        vTaskDelay(1);
    }
}

void keyboard_setup()
{
    usbh_setup(show_config_desc_full);
    xTaskCreatePinnedToCore(keyboard_task, "keyboard_task", 6144, NULL, 2, &TASK_HOST_KEYBOARD_HANDLE, 1);
}


// ESP_LOGI("", "HID report: %02x %02x %02x %02x %02x %02x %02x %02x",
//          p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);

/*
    UP:00 00 52 00 00 00 00 00
    DOWN:00 00 51 00 00 00 00 00
    LEFT:00 00 50 00 00 00 00 00
    RIGHT:00 00 4f 00 00 00 00 00

    W:00 00 1a 00 00 00 00 00
    S:00 00 16 00 00 00 00 00
    A:00 00 04 00 00 00 00 00
    D:00 00 07 00 00 00 00 00

    J:00 00 0d 00 00 00 00 00
    K:00 00 0e 00 00 00 00 00
    U:00 00 18 00 00 00 00 00
    I:00 00 0C 00 00 00 00 00

    F1:00 00 3a 00 00 00 00 00
    F2:00 00 3b 00 00 00 00 00

    组合测试：ASD:00 00 04 16 07 00 00 00
    ASW:00 00 01 01 01 01 01 01

*/
