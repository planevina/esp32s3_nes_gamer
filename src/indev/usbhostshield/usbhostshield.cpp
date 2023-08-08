#include "usbhostshield.h"
#include "config.h"
#include "../controller.h"
#include "nvs_flash.h"
#include <SPI.h>

#ifdef USB_HOST_SHIELD_ENABLED

#ifdef USB_HID_JOY_ENABLED
#include <usbhid.h>
#include <hiduniversal.h>
#include <usbhub.h>

#include "hidjoystickrptparser.h"

USB Usb;
USBHub Hub(&Usb);
HIDUniversal Hid(&Usb);
JoystickEvents JoyEvents;
JoystickReportParser Joy(&JoyEvents);

uint8_t uhs_get_key_value()
{
    uint8_t value = 0;

    return value;
}

#endif

#ifdef USB_PS4_JOY_ENABLED
#include <PS4USB.h>

USB Usb;
PS4USB PS4(&Usb);

uint8_t uhs_get_key_value()
{
    uint8_t value = 0;
    if (!PS4.connected())
    {
        return value;
    }

    if (PS4.getButtonPress(TRIANGLE) || PS4.getButtonPress(SQUARE))
    {
        value |= GAMEPAD_KEY_B;
    }
    if (PS4.getButtonPress(CIRCLE) || PS4.getButtonPress(CROSS))
    {
        value |= GAMEPAD_KEY_A;
    }

    if (PS4.getButtonPress(UP))
    {
        value |= GAMEPAD_KEY_UP;
    }
    if (PS4.getButtonPress(RIGHT))
    {
        value |= GAMEPAD_KEY_RIGHT;
    }
    if (PS4.getButtonPress(DOWN))
    {
        value |= GAMEPAD_KEY_DOWN;
    }
    if (PS4.getButtonPress(LEFT))
    {
        value |= GAMEPAD_KEY_LEFT;
    }

    if (PS4.getButtonPress(SHARE))
    {
        value |= GAMEPAD_KEY_SELECT;
    }

    if (PS4.getButtonPress(OPTIONS))
    {
        value |= GAMEPAD_KEY_START;
    }
    return value;
}
#endif

#ifdef WIRELESS_XBOX_ONE_JOY_ENABLED
#include <XBOXRECV.h>

USB Usb;
XBOXRECV Xbox(&Usb);


uint8_t uhs_get_key_value()
{
    uint8_t value = 0;
    if (!Xbox.XboxReceiverConnected)
    {
        return value;
    }
    if(!Xbox.Xbox360Connected[0])
    {
        return value;
    }

    if (Xbox.getButtonPress(UP ,0))
    {
        value |= GAMEPAD_KEY_UP;
    }

    if (Xbox.getButtonPress(DOWN,0))
    {
        value |= GAMEPAD_KEY_DOWN;
    }

    if (Xbox.getButtonPress(LEFT,0))
    {
        value |= GAMEPAD_KEY_LEFT;
    }

    if (Xbox.getButtonPress(RIGHT,0))
    {
        value |= GAMEPAD_KEY_RIGHT;
    }

    if (Xbox.getButtonPress(START,0))
    {
        value |= GAMEPAD_KEY_START;
    }

    if (Xbox.getButtonPress(BACK,0))
    {
        value |= GAMEPAD_KEY_SELECT;
    }

    if (Xbox.getButtonPress(A,0) || Xbox.getButtonPress(B,0))
    {
        value |= GAMEPAD_KEY_A;
    }

    if (Xbox.getButtonPress(X,0) || Xbox.getButtonPress(Y,0))
    {
        value |= GAMEPAD_KEY_B;
    }

    return value;
}
#endif

#ifdef USB_XBOX_ONE_JOY_ENABLED
#include <XBOXONE.h>
USB Usb;
XBOXONE Xbox(&Usb);

uint8_t uhs_get_key_value()
{
    uint8_t value = 0;
    if (!Xbox.XboxOneConnected)
    {
        return value;
    }

    if (Xbox.getButtonPress(UP))
    {
        value |= GAMEPAD_KEY_UP;
    }

    if (Xbox.getButtonPress(DOWN))
    {
        value |= GAMEPAD_KEY_DOWN;
    }

    if (Xbox.getButtonPress(LEFT))
    {
        value |= GAMEPAD_KEY_LEFT;
    }

    if (Xbox.getButtonPress(RIGHT))
    {
        value |= GAMEPAD_KEY_RIGHT;
    }

    if (Xbox.getButtonPress(START))
    {
        value |= GAMEPAD_KEY_START;
    }

    if (Xbox.getButtonPress(BACK))
    {
        value |= GAMEPAD_KEY_SELECT;
    }

    if (Xbox.getButtonPress(A) || Xbox.getButtonPress(B))
    {
        value |= GAMEPAD_KEY_A;
    }

    if (Xbox.getButtonPress(X) || Xbox.getButtonPress(Y))
    {
        value |= GAMEPAD_KEY_B;
    }

    return value;
}
#endif

#ifdef BT_PS4_JOY_ENABLED
#include <PS4BT.h>
#include <usbhub.h>

USB Usb;
//USBHub Hub1(&Usb); // Some dongles have a hub inside
BTD Btd(&Usb); // You have to create the Bluetooth Dongle instance like so

/* You can create the instance of the PS5BT class in two ways */
// This will start an inquiry and then pair with the PS5 controller - you only have to do this once
// You will need to hold down the PS and Share button at the same time, the PS5 controller will then start to blink rapidly indicating that it is in pairing mode
PS4BT PS4(&Btd,PAIR);

// After that you can simply create the instance like so and then press the PS button on the device
//PS4BT PS4(&Btd);
uint8_t uhs_get_key_value()
{
    uint8_t value = 0;
    if (!PS4.connected())
    {
        return value;
    }

    if (PS4.getButtonPress(TRIANGLE) || PS4.getButtonPress(SQUARE))
    {
        value |= GAMEPAD_KEY_B;
    }
    if (PS4.getButtonPress(CIRCLE) || PS4.getButtonPress(CROSS))
    {
        value |= GAMEPAD_KEY_A;
    }

    if (PS4.getButtonPress(UP))
    {
        value |= GAMEPAD_KEY_UP;
    }
    if (PS4.getButtonPress(RIGHT))
    {
        value |= GAMEPAD_KEY_RIGHT;
    }
    if (PS4.getButtonPress(DOWN))
    {
        value |= GAMEPAD_KEY_DOWN;
    }
    if (PS4.getButtonPress(LEFT))
    {
        value |= GAMEPAD_KEY_LEFT;
    }

    if (PS4.getButtonPress(SHARE))
    {
        value |= GAMEPAD_KEY_SELECT;
    }

    if (PS4.getButtonPress(OPTIONS))
    {
        value |= GAMEPAD_KEY_START;
    }
    return value;
}

#endif

TaskHandle_t TASK_HOST_SHIELD_HANDLE;

void uhs_init()
{
    SHOW_MSG_SERIAL("Initializing USB Host...")
#ifdef UHS_RST
    pinMode(UHS_RST,OUTPUT);
    digitalWrite(UHS_RST,HIGH);
    delay(1000);
#endif
    if (Usb.Init() == -1)
    {
        SHOW_MSG_SERIAL("[ FAIL ]\n")
        vTaskDelete(NULL);
    }
    SHOW_MSG_SERIAL("[ OK ]\n")
#ifdef USB_HID_JOY_ENABLED
    vTaskDelay(200);
    if (!Hid.SetReportParser(0, &Joy))
        ErrorMessage<uint8_t>(PSTR("SetReportParser"), 1);
#endif
}

void task_host_shield(void *pvParameters)
{
    uhs_init();
    for (;;)
    {
        Usb.Task();
        vTaskDelay(1);
    }
}
#endif