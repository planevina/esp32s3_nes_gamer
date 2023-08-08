#include "main.h"
#include "network.h"
#include "nvstools.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include "nvs_flash.h"
#include <DNSServer.h>
#include <WebServer.h>
#include "ConfigWifiHtml.h"

TaskHandle_t TASK_NETWORK_HANDLE;

static WebServer server(80);           // 创建dnsServer实例
static DNSServer dnsServer;            // dnsServer
static IPAddress apIP(192, 168, 4, 1); // AP-IP地址
static int connected = 0;              // WIFI连接计数器，(每次连接和断开时递增)
static unsigned long waitTime = 60000; // 每次连接和断开时等待增量（毫秒）
static unsigned long alarmConnect = 0; // 之后再次尝试WiFi连接的时间

const int MAX_SSID = 32;
const int MAX_PWD = 64;
struct
{
    char ssid[MAX_SSID + 1] = "";
    char pwd[MAX_PWD + 1] = "";
} WiFiData;

void loadWiFiConfigFromNVS()
{
    nvs_handle_t wifiNVS;
    esp_err_t err;
    err = nvs_open("WIFI", NVS_READWRITE, &wifiNVS);
    if (err != ESP_OK)
    {
        SHOW_MSG_SERIAL("Error opening WIFI NVS handle!\n");
        save_cfg.wifiMode = 0;
    }
    else
    {
        size_t len;
        if (nvs_get_blob(wifiNVS, "WIFIDATA", NULL, &len))
        {
            SHOW_MSG_SERIAL("[WIFI] No Wifi data.\n");
            save_cfg.wifiMode = 0;
        }
        else
        {
            nvs_get_blob(wifiNVS, "WIFIDATA", &WiFiData, &len);
            if (strlen(WiFiData.ssid) > 0)
            {
                SHOW_MSG_SERIAL("[WIFI] Load saved wifi data.\n");
                save_cfg.wifiMode = 1;
            }
            else
            {
                SHOW_MSG_SERIAL("[WIFI] Load empty wifi data.\n");
                save_cfg.wifiMode = 0;
            }
        }
    }
    nvs_close(wifiNVS);
}

void saveWifiData()
{
    nvs_handle_t wifiNVS;
    esp_err_t err;
    err = nvs_open("WIFI", NVS_READWRITE, &wifiNVS);
    if (err != ESP_OK)
    {
        SHOW_MSG_SERIAL("Error opening WIFI NVS handle!\n");
    }
    else
    {
        nvs_set_blob(wifiNVS, "WIFIDATA", &WiFiData, sizeof(WiFiData));
        nvs_commit(wifiNVS);
        SHOW_MSG_SERIAL("[NVS] Save wifi data.\n");
    }
    nvs_close(wifiNVS);
}

void clearWifiData()
{
    nvs_handle_t wifiNVS;
    esp_err_t err;
    err = nvs_open("WIFI", NVS_READWRITE, &wifiNVS);
    if (err != ESP_OK)
    {
        SHOW_MSG_SERIAL("Error opening WIFI NVS handle!\n");
    }
    else
    {
        nvs_erase_key(wifiNVS, "WIFIDATA");
        nvs_commit(wifiNVS);
        SHOW_MSG_SERIAL("[NVS] Erase wifi data.\n");
    }
    nvs_close(wifiNVS);
}

void handleIndex()
{
    server.send(200, "text/html", page_html);
}

//  尝试配网
void handleConfigWifi()
{
    WiFi.setAutoConnect(true);                                   // 设置自动连接
    WiFi.setAutoReconnect(true);                                 // 设置断开连接后重连
    server.arg("ssid").toCharArray(WiFiData.ssid, MAX_SSID + 1); // 长度超过了会被截取
    server.arg("pwd").toCharArray(WiFiData.pwd, MAX_PWD + 1);
    WiFi.begin(WiFiData.ssid, WiFiData.pwd); // 使用配网获取的wifi信息
    int count = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
        vTaskDelay(500);
        count++;
        if (count > 40) // 重试20秒
        {
            server.send(200, "text/html", "wifi连接失败,请检查密码后重试。"); // 返回保存成功页面
            break;
        }
    }
    if (WiFi.status() == WL_CONNECTED) // 如果连接上 就输出IP信息
    {
        SHOW_MSG_SERIAL("[WIFI] WiFi Connected:");
        SHOW_MSG_SERIAL(WiFi.localIP());
        SHOW_MSG_SERIAL("\n");
        server.send(200, "text/html", "wifi连接成功,即将重启设备。");
        saveWifiData();
        save_cfg.wifiMode = 1;
        save_config();
        delay(2000);
        ESP.restart();
        // 这里需要显示+重启
    }
}

// 扫描可用wifi
void handleWifiList()
{
    int n = WiFi.scanNetworks(); // 开始同步扫描，将返回值存放在变量n中
    if (n > 0)                   // 只有有数据的时候才处理
    {
        char wifilist[640] = {0}; // 返回给网页的数据
        SHOW_MSG_SERIAL("[WIFI] Scan WiFi...\n");
        for (int i = 0; i < 20; ++i) // 开始逐个打印扫描到的
        {
            sprintf(wifilist, "%s%s%s", wifilist, WiFi.SSID(i).c_str(), ","); // 组装信息返回给接口
        }
        SHOW_MSG_SERIAL(wifilist); // 打印一下日志
        SHOW_MSG_SERIAL("\n");
        server.send(200, "text/html", wifilist); // 返回保存成功页面
        return;                                  // 结束这里的操作
    }
    SHOW_MSG_SERIAL("[WIFI] No WiFi.\n");     // 打印没有任何wifi日志
    server.send(200, "text/html", ".nodata"); // 返回保存成功页面
}

void initWebServer()
{
    WiFi.mode(WIFI_AP_STA);                                     // 设置模式为wifi热点模式
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0)); // 初始化AP模式
    WiFi.softAP(AP_SSID, AP_PWD, 1, 0, 4);                      // 初始化AP模式
    server.on("/", HTTP_GET, handleIndex);                      // 设置主页回调函数
    server.on("/configwifi", HTTP_GET, handleConfigWifi);       // 设置Post请求回调函数
    server.on("/wifilist", HTTP_GET, handleWifiList);           // 设置获取wifi列表回调函数
    server.onNotFound(handleIndex);                             // 设置无法响应的http请求的回调函数
    server.begin();                                             // 启动WebServer
    SHOW_MSG_SERIAL("[WIFI] WebServer started!\n");
    dnsServer.start(53, "*", apIP);
}

void ap_loop()
{
    server.handleClient();
    dnsServer.processNextRequest();
}

void checkWiFi()
{
    if (connected % 2)
    {
        // 第二次执行才会到这里
        if (WiFi.status() == WL_CONNECTED)
            return;

        SHOW_MSG_SERIAL("[WIFI] WiFi Connection Lost!");
        connected++;
        waitTime = 60000;
        alarmConnect = 0;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        if (millis() < alarmConnect)
            return;

        if (waitTime == 60000)
            waitTime = 1000;
        else
            waitTime *= 2;

        if (waitTime == 32000)
        {
            SHOW_MSG_SERIAL("\nCan't connect to ");
            SHOW_MSG_SERIAL(WiFiData.ssid);
            SHOW_MSG_SERIAL("\nWill try connecting again in 60 seconds.\n\n");
            waitTime = 60000;
        }
        else
        {
            SHOW_MSG_SERIAL("[WIFI] Trying to connect to ");
            SHOW_MSG_SERIAL(WiFiData.ssid);
            SHOW_MSG_SERIAL(".  Waiting ");
            SHOW_MSG_SERIAL(waitTime / 1000);
            SHOW_MSG_SERIAL("sec...\n");
            WiFi.begin(WiFiData.ssid, WiFiData.pwd);
        }
        alarmConnect = millis() + waitTime;
        return;
    }

    // 如果连不上wifi就不会到后面来
    connected++;
    SHOW_MSG_SERIAL("[WIFI] WiFi Connected!  IP Address = ");
    SHOW_MSG_SERIAL(WiFi.localIP().toString().c_str());
    SHOW_MSG_SERIAL("\n");

    if (connected > 1) // 说明是重连
    {
        return;
    }
    else
    {
        // 连上了

    }
}

bool get_wifi_status()
{
    return WiFi.status() == WL_CONNECTED;
}

void task_network(void *pvParameters)
{
    if (save_cfg.wifiMode)
    {
        loadWiFiConfigFromNVS();
    }
    
    //如果读取后没获取到保存的信息
    if (save_cfg.wifiMode)
    {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(cfg.controller == CONTROLLER_WECHAT_BLEPAD);
    }
    else
    {
        initWebServer(); // 初始化配网服务器
    }

    for (;;)
    {
        if (save_cfg.wifiMode)
        {
            checkWiFi(); // 连接wifi和检查wifi连接状态
        }
        else
        {
            ap_loop();
        }
        vTaskDelay(10);
    }
}