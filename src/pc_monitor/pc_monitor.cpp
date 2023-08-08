#include "pc_monitor.h"
#include "main.h"
#include "../network.h"
#include <WiFi.h>

const uint8_t pc_data_num = 17; // 一共有多少个数据
// 数据解析表 - 前导字符串
const char *rs_data_header[] = {
    "CPU usage",
    "CPU temp",
    "CPU freq",
    "CPU fan",
    "GPU usage",
    "GPU temp",
    "GPU freq",
    "GPU fan",
    "RAM usage",
    "RAM avl",
    "NET up",
    "NET down",
    "D1 act",
    "D1 temp",
    "D2 act",
    "D2 temp",
    "MB temp"};

#define PC_DATA_END_CHAR '^'           // 每个字段的结尾分隔符
static WiFiClient wifi_client;         // wifi客户端
aida64_pc_data pc_data = {0};          // 电脑信息

IPAddress pc_host;

bool pc_mon_init()
{
    if (save_cfg.ipAddrBytes[0] != 0 && save_cfg.ipAddrBytes[0] != 0xff && save_cfg.ipAddrBytes[1] != 0 && save_cfg.ipAddrBytes[1] != 0xff && save_cfg.ipAddrBytes[2] != 0 && save_cfg.ipAddrBytes[2] != 0xff && save_cfg.ipAddrBytes[3] != 0 && save_cfg.ipAddrBytes[3] != 0xff)
    {
        pc_host = IPAddress(save_cfg.ipAddrBytes);
        return true;
    }
    else
    {
        return false;
    }
}

void proc_pc_data(String line)
{
    int16_t dataStart = 0;
    int16_t dataEnd = 0;
    String dataStr;
    int data[pc_data_num];

    // 解析数据
    for (int i = 0; i < pc_data_num; i++)
    {
        dataStart = line.indexOf(rs_data_header[i]) + strlen(rs_data_header[i]); // 寻找前导字符串
        dataEnd = line.indexOf(PC_DATA_END_CHAR, dataStart);                     // 不需要单位字符串
        dataStr = line.substring(dataStart, dataEnd);
        if (i == 10 || i == 11)
        {
            // 网速这里是浮点数
            data[i] = dataStr.toFloat() * 10;
        }
        else
        {
            data[i] = dataStr.toInt();
        }
    }

    pc_data.cpu_usage = data[0];
    pc_data.cpu_temp = data[1];
    pc_data.cpu_freq = data[2];
    pc_data.cpu_fan = data[3];

    pc_data.gpu_usage = data[4];
    pc_data.gpu_temp = data[5];
    pc_data.gpu_freq = data[6];
    pc_data.gpu_fan = data[7];

    pc_data.ram_usage = data[8];
    pc_data.ram_avl = data[9];

    pc_data.net_upload_speed = data[10];
    pc_data.net_download_speed = data[11];

    pc_data.disk1_usage = data[12];
    pc_data.disk1_temp = data[13];

    pc_data.disk2_usage = data[14];
    pc_data.disk2_temp = data[15];

    pc_data.mb_temp = data[16];
    pc_data.update_ts = millis();
}

bool get_pc_data(void)
{
    if (!get_wifi_status())
        return false;

    if (!wifi_client.connect(pc_host, 80, 200))
    {
        SHOW_MSG_SERIAL("Connect host: ")
        SHOW_MSG_SERIAL(pc_host)
        SHOW_MSG_SERIAL(" failed!\n");
        return false;
    }

    String getUrl = "/sse";
    wifi_client.print(String("GET ") + getUrl + " HTTP/1.1\r\n" +
                      "Content-Type=application/json;charset=utf-8\r\n" + "Host: " +
                      pc_host.toString() + "\r\n" + "User-Agent=ESP32\r\n" + "Connection: close\r\n\r\n");

    delay(10);

    char endOfHeaders[] = "\n\n";
    bool ok = wifi_client.find(endOfHeaders);
    if (!ok)
    {
        SHOW_MSG_SERIAL("No response or invalid response!\n");
        return false;
    }
    String line = "";
    line += wifi_client.readStringUntil('\n');
    wifi_client.stop();

    // 解析数据
    proc_pc_data(line);
    return true;
}
