#include "blegamepad.h"
#include "config.h"
#include "../controller.h"

#include "nvstools.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

static uint8_t keyvalue=0;
#define SERVICE_UUID "EF933FD3-F6F2-E572-EF3D-928AB415CB0E"

// RX串口标识
#define CHARACTERISTIC_UUID_RX "C02E69C2-E503-43F8-A74D-B95C1F5AF088"
// TX串口标识
#define CHARACTERISTIC_UUID_TX "D914E6B6-509C-4803-9FB5-9454782478A6"

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic;
TaskHandle_t TASK_BLE_HANDLE;

static bool deviceConnected = false;
static bool oldDeviceConnected = false;
static uint8_t * ble_rcv_char = NULL;
static size_t ble_rcv_size = 0;
static bool isBleInited = false;

class MyServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *pServer)
    {
        deviceConnected = true;
        vTaskDelay(2);
    };

    void onDisconnect(BLEServer *pServer)
    {
        deviceConnected = false;
        vTaskDelay(2);
    }
};

class MyCallbacks : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *pCharacteristic)
    {
        ble_rcv_char = pCharacteristic->getData();
        ble_rcv_size = pCharacteristic->getLength();
    }
};

void init_ble()
{
    // 初始化蓝牙设备
    SHOW_MSG_SERIAL("Initializing BLE...");
    char blename[20] = {0};
    load_devicename(blename);
    BLEDevice::init(blename);
    // 为蓝牙设备创建服务器
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    // 基于SERVICE_UUID来创建一个服务
    BLEService *pService = pServer->createService(SERVICE_UUID);
    pTxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_TX,
        BLECharacteristic::PROPERTY_NOTIFY);
    pTxCharacteristic->addDescriptor(new BLE2902());
    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_RX,
        BLECharacteristic::PROPERTY_WRITE);
    pRxCharacteristic->setCallbacks(new MyCallbacks());
    // 开启服务
    pService->start();
    // pServer->startAdvertising();
    BLEAdvertising *pAdvertising = pServer->getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->start();
    SHOW_MSG_SERIAL("[ OK ]\n");
    isBleInited = true;
}

void start_ble()
{
    // pServer->getAdvertising()->start();
    pServer->startAdvertising();
    SHOW_MSG_SERIAL("[BLE] Started.\n");
}

// 蓝牙处理逻辑
void ble_proc()
{
    if (ble_rcv_size == 20)
    {
        SHOW_MSG_SERIAL("[BLE] Recieved command\n");
        if (ble_rcv_char[0] == 0xBB && ble_rcv_char[1] == 0xAB)
        {
            char a[18] = {0};
            for(int i=0;i<18;i++)
            {
                a[i] = ble_rcv_char[i+2];
            }
            save_devicename(a);
        }
        return;
    }
    
    
    if (ble_rcv_size != 4)
    {
        SHOW_MSG_SERIAL("[BLE] Wrong data recieved\n");
        return;
    }
    
    if (ble_rcv_char[0] != 0xBB || ble_rcv_char[3] != 0xEE)
    {
        SHOW_MSG_SERIAL("[BLE] Wrong begin and end char\n");
        return;
    }
    keyvalue = ble_rcv_char[2];
    return;
}

uint8_t wxpad_get_key_value()
{
    return keyvalue;
}

void ble_gamepad_init()
{
    xTaskCreatePinnedToCore(task_ble, "task_ble", 8192, NULL, 2, &TASK_BLE_HANDLE, 1);
}

void ble_loop()
{
    if (deviceConnected && ble_rcv_size > 0)
    {
        ble_proc();
        ble_rcv_char = NULL;
        ble_rcv_size = 0;
    }
    if (!deviceConnected && oldDeviceConnected) // 没有新连接时
    {
        vTaskDelay(500);
        start_ble(); // 重新开始广播
        oldDeviceConnected = deviceConnected;
    }
    if (deviceConnected && !oldDeviceConnected) // 正在连接时
    {
        oldDeviceConnected = deviceConnected;
        SHOW_MSG_SERIAL("[BLE] Client connected\n");
    }
}

void task_ble(void *pvParameters)
{
    init_ble();
    for (;;)
    {
        ble_loop();
        vTaskDelay(1);
    }
}

