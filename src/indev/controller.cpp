#include "controller.h"
#include "main.h"

// USB Host Shield
#ifdef USB_A_PORT_MODE_UHS
#include "usbhostshield/usbhostshield.h"
#endif

#ifdef USB_A_PORT_MODE_CDC
#include "keyboard/usbkeyboard.h"
#include "joystick/joystick.h"
#endif

// 自带GPIO手柄
#ifdef GPIO_PAD_ENABLED
#include "gpiopad/gpiopad.h"
#endif

#include "blegamepad/blegamepad.h"

nes_pad_key_s gamepad_p1 = {0};
nes_pad_key_s gamepad_p2 = {0};

uint8_t get_pad0_value()
{
    uint8_t value = 0;

    // 多个手柄是并集模式
    //  TODO  映射1p 2p
#ifdef GPIO_PAD_ENABLED
    value = gpio_get_key_value();
#endif

    if (cfg.controller == CONTROLLER_WECHAT_BLEPAD)
        value |= wxpad_get_key_value();

#ifdef USB_A_PORT_MODE_UHS
    value |= uhs_get_key_value();
#endif

#ifdef USB_A_PORT_MODE_CDC
    if (cfg.controller == CONTROLLER_USB_HID_KBD)
        value |= keyboard_get_key_value();
    else if (cfg.controller == CONTROLLER_USB_HID_JOYSTICK)
        value |= joystick_get_key_value();
#endif

    return value;
}

uint8_t get_pad1_value()
{
    uint8_t value = 0;
#ifdef USB_A_PORT_MODE_CDC
    if (cfg.controller == CONTROLLER_USB_HID_KBD)
        value |= keyboard_p2_get_key_value();
#endif
    return value;
}

void input_init()
{

#ifdef GPIO_PAD_ENABLED
    gpio_pad_init();
#endif

    if (cfg.controller == CONTROLLER_WECHAT_BLEPAD)
        ble_gamepad_init();
#ifdef USB_A_PORT_MODE_UHS
    xTaskCreatePinnedToCore(task_host_shield, "task_usb_host", 8196, NULL, 2, &TASK_HOST_SHIELD_HANDLE, 1);
#endif

#ifdef USB_A_PORT_MODE_CDC
    if (cfg.controller == CONTROLLER_USB_HID_KBD)
        keyboard_setup();
    else if (cfg.controller == CONTROLLER_USB_HID_JOYSTICK)
        joystick_init();
#endif
}

void input_clear()
{
    gamepad_p1 = {0};
    gamepad_p2 = {0};
}

void input_refresh()
{
    gamepad_p1.KEY_VALUE = get_pad0_value();
    gamepad_p2.KEY_VALUE = get_pad1_value();
}
