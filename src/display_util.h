#ifndef _DISP_UTIL_H
#define _DISP_UTIL_H
#include "stdint.h"

uint8_t get_num_width(uint32_t num)
{
    if (num < 10)
        return 1;
    else if (num < 100)
        return 2;
    else if (num < 1000)
        return 3;
    else if (num < 10000)
        return 4;
    else if (num < 100000)
        return 5;
    else if (num < 1000000)
        return 6;
    else if (num < 10000000)
        return 7;
    else if (num < 100000000)
        return 8;
    else if (num < 1000000000L)
        return 9;
    else
        return 10;
}

uint8_t get_num_width(uint16_t num)
{
    if (num < 10)
        return 1;
    else if (num < 100)
        return 2;
    else if (num < 1000)
        return 3;
    else if (num < 10000)
        return 4;
    else
        return 5;
}

uint8_t get_num_width(uint8_t num)
{
    if (num < 10)
        return 1;
    else if (num < 100)
        return 2;
    else
        return 3;
}

uint8_t get_num_width(int8_t num)
{
    int8_t x = num;
    uint8_t w = 0;
    if (x < 0)
    {
        w = 1;
        x = -x;
    }
    if (x < 10)
        return w + 1;
    else if (num < 100)
        return w + 2;
    else
        return w + 3;
}

#endif