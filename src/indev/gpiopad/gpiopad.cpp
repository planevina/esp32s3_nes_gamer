#include "gpiopad.h"
#include "config.h"
#include "../controller.h"

#ifdef GPIO_PAD_ENABLED

static uint16_t xMin = 60;      // x轴最小
static uint16_t yMin = 53;      // y轴最小
static uint16_t xCenter = 1900; // x轴中心
static uint16_t yCenter = 1900; // y轴中心
static uint16_t xMax = 4095;    // x轴Max值
static uint16_t yMax = 4095;    // y轴Max值
static uint16_t deadZone = 1000; //中心死区
static int xVal, yVal;

uint8_t gpio_get_key_value()
{
    uint8_t value = 0;
    if (digitalRead(PIN_A) == LOW)
        value |= GAMEPAD_KEY_A; // A
    if (digitalRead(PIN_B) == LOW)
        value |= GAMEPAD_KEY_B; // B
    if (digitalRead(PIN_SELECT) == LOW)
        value |= GAMEPAD_KEY_SELECT; // SELECT
    if (digitalRead(PIN_START) == LOW)
        value |= GAMEPAD_KEY_START; // START

    xVal = analogRead(ADC_X);
    yVal = analogRead(ADC_Y);

    if (xVal < xCenter - deadZone)
    {
        value |= GAMEPAD_KEY_LEFT;
    }
    else if (xVal > xCenter + deadZone)
    {
        value |= GAMEPAD_KEY_RIGHT;
    }

    if (yVal < yCenter - deadZone)
    {
        value |= GAMEPAD_KEY_DOWN;
    }
    else if (yVal > yCenter + deadZone)
    {
        value |= GAMEPAD_KEY_UP;
    }
    return value;
}

void get_gpio_pad_status()
{
    xVal = analogRead(ADC_X);
    yVal = analogRead(ADC_Y);

    Serial.printf("x:%d,y:%d\n",xVal,yVal);

}

void gpio_pad_init()
{
    analogSetPinAttenuation(ADC_X, ADC_ATTENDB_MAX);
    analogSetPinAttenuation(ADC_Y, ADC_ATTENDB_MAX);
    pinMode(PIN_A,INPUT_PULLUP);
    pinMode(PIN_B,INPUT_PULLUP);
    pinMode(PIN_SELECT,INPUT_PULLUP);
    pinMode(PIN_START,INPUT_PULLUP);
}


#endif
