#include <Arduino.h>
#include "main.h"
#include "nvstools.h"
#include "nvs_flash.h"

void clear_devicename()
{
    esp_err_t err;
    nvs_handle_t st_handle;
    err = nvs_open(NVS_STOR_NAME, NVS_READWRITE, &st_handle);
    if (err != ESP_OK)
    {
        SHOW_MSG_SERIAL("Error opening BLE NVS handle!\n");
    }
    else
    {
        nvs_erase_key(st_handle, "BLENAME");
        nvs_commit(st_handle);
    }
    nvs_close(st_handle);
}

void save_devicename(char *name)
{
    esp_err_t err;
    nvs_handle_t st_handle;
    err = nvs_open(NVS_STOR_NAME, NVS_READWRITE, &st_handle);
    if (err != ESP_OK)
    {
        SHOW_MSG_SERIAL("Error opening BLE NVS handle!\n");
    }
    else
    {
        err = nvs_set_str(st_handle, "BLENAME", name);
        if (err == ESP_OK)
        {
            err = nvs_commit(st_handle);
            if (err != ESP_OK)
                SHOW_MSG_SERIAL("Save ble name commit Failed!\n");
        }
        else
        {
            SHOW_MSG_SERIAL("Save ble name err\n");
        }
    }
    nvs_close(st_handle);
}

void load_devicename(char *ble_name)
{
    // 读取存储的设备名称
    nvs_handle_t st_handle;
    esp_err_t err;
    err = nvs_open(NVS_STOR_NAME, NVS_READWRITE, &st_handle);
    if (err != ESP_OK)
    {
        SHOW_MSG_SERIAL("Error opening BLE NVS handle!\n");
        snprintf(ble_name, 20, "%s", DEVICE_BLE_NAME);
    }
    else
    {
        size_t name_size;
        err = nvs_get_str(st_handle, "BLENAME", NULL, &name_size);
        if (err != ESP_OK)
        {
            //SHOW_MSG_SERIAL("[NVS] Error get BLENAME size!\n");
            snprintf(ble_name, 20, "%s", DEVICE_BLE_NAME);
        }
        else
        {
            if (name_size > 18)
                name_size = 18;
            err = nvs_get_str(st_handle, "BLENAME", ble_name, &name_size);
            if (err != ESP_OK)
            {
                SHOW_MSG_SERIAL("[NVS] Error get BLENAME!\n");
                snprintf(ble_name, 20, "%s", DEVICE_BLE_NAME);
            }
            else
            {
                // 获取到了
                SHOW_MSG_SERIAL("[NVS] Use custom device name\n");
            }
        }
    }
    nvs_close(st_handle);
}

bool clear_auto_play(uint8_t mode)
{
    esp_err_t err;
    nvs_handle_t st_handle;
    bool re = false;
    char *key = NULL;
    if (mode == MY_APP_NES)
    {
        key = (char *)"AUTOGAME";
    }
    else if (mode == MY_APP_MJPEG)
    {
        key = (char *)"AUTOMJPEG";
    }
    else
    {
        return re;
    }
    err = nvs_open(NVS_STOR_NAME, NVS_READWRITE, &st_handle);
    if (err != ESP_OK)
    {
        SHOW_MSG_SERIAL("Error opening NVS handle!\n");
    }
    else
    {
        nvs_erase_key(st_handle, key);
        nvs_commit(st_handle);
        re = true;
    }
    nvs_close(st_handle);
    return re;
}

bool save_auto_play(char *file_name, uint8_t mode)
{
    esp_err_t err;
    nvs_handle_t st_handle;
    bool re = false;
    char *key = NULL;
    if (mode == MY_APP_NES)
    {
        key = (char *)"AUTOGAME";
    }
    else if (mode == MY_APP_MJPEG)
    {
        key = (char *)"AUTOMJPEG";
    }
    else
    {
        return re;
    }
    err = nvs_open(NVS_STOR_NAME, NVS_READWRITE, &st_handle);
    if (err != ESP_OK)
    {
        SHOW_MSG_SERIAL("Error opening NVS handle!\n");
    }
    else
    {
        err = nvs_set_str(st_handle, key, file_name);
        if (err == ESP_OK)
        {
            err = nvs_commit(st_handle);
            if (err != ESP_OK)
            {
                SHOW_MSG_SERIAL("Save auto play failed!\n");
            }
            else
            {
                re = true;
            }
        }
        else
        {
            SHOW_MSG_SERIAL("Save auto play error\n");
        }
    }
    nvs_close(st_handle);
    return re;
}

bool load_auto_play(char *file_name, uint8_t mode)
{
    // 读取存储的设备名称
    nvs_handle_t st_handle;
    esp_err_t err;
    bool re = false;
    char *key = NULL;
    if (mode == MY_APP_NES)
    {
        key = (char *)"AUTOGAME";
    }
    else if (mode == MY_APP_MJPEG)
    {
        key = (char *)"AUTOMJPEG";
    }
    else
    {
        return false;
    }
    err = nvs_open(NVS_STOR_NAME, NVS_READWRITE, &st_handle);
    if (err != ESP_OK)
    {
        SHOW_MSG_SERIAL("Error opening NVS handle!\n");
    }
    else
    {
        size_t name_size;
        err = nvs_get_str(st_handle, key, NULL, &name_size);
        if (err != ESP_OK)
        {
            //SHOW_MSG_SERIAL("[NVS] Error get auto info!\n");
        }
        else
        {
            if (name_size > MAXFILENAME_LENGTH)
                name_size = MAXFILENAME_LENGTH;
            err = nvs_get_str(st_handle, key, file_name, &name_size);
            if (err != ESP_OK)
            {
                SHOW_MSG_SERIAL("[NVS] Error get auto filename!\n");
            }
            else
            {
                // 获取到了
                SHOW_MSG_SERIAL("[NVS] Auto play filename loaded.\n");
                re = true;
            }
        }
    }
    nvs_close(st_handle);
    return re;
}



void save_config()
{
    nvs_handle_t st_handle;
    esp_err_t err;

    err = nvs_open(NVS_STOR_NAME, NVS_READWRITE, &st_handle);
    if (err != ESP_OK)
    {
        SHOW_MSG_SERIAL("Error opening NVS handle!\n");
        return;
    }
    nvs_set_blob(st_handle, "LBWCFGDATA", &save_cfg, sizeof(save_cfg));
    nvs_commit(st_handle);
    nvs_close(st_handle);
}

void set_auto_mode(uint8_t mode)
{
    if(save_cfg.favMode == mode)
    {
        //清除
        save_cfg.favMode = MY_APP_NONE;
    }
    else
    {
        //保存
        save_cfg.favMode = mode;
    }
    save_config();
}

void load_config()
{
    nvs_handle_t st_handle;
    esp_err_t err;

    err = nvs_open(NVS_STOR_NAME, NVS_READWRITE, &st_handle);
    if (err != ESP_OK)
    {
        SHOW_MSG_SERIAL("Error opening NVS handle!\n");
        return;
    }
    size_t len;
    if (nvs_get_blob(st_handle, "LBWCFGDATA", NULL, &len))
    {
        // 返回错误代码不为0
        SHOW_MSG_SERIAL("Use default cfg.\n");
    }
    else
    {
        if (nvs_get_blob(st_handle, "LBWCFGDATA", &save_cfg, &len))
        {
            SHOW_MSG_SERIAL("Load failed,use default cfg.\n");
        }
        else
        {
            cfg.currMode = save_cfg.favMode;
            cfg.mjpegPlayMode = save_cfg.mjpegPlayMode;
            cfg.mute = save_cfg.mute;
            cfg.controller = save_cfg.controller;
            SHOW_MSG_SERIAL("Use saved cfg.\n");
        }
    }
    nvs_close(st_handle);
}

void clear_config()
{
    esp_err_t err;
    nvs_handle_t st_handle;
    err = nvs_open(NVS_STOR_NAME, NVS_READWRITE, &st_handle);
    if (err != ESP_OK)
    {
        SHOW_MSG_SERIAL("Error opening BLE NVS handle!\n");
    }
    else
    {
        nvs_erase_all(st_handle);
    }
    nvs_close(st_handle);
}