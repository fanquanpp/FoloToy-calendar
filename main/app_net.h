// main/app_net.h —— 联网 / 配网 / 校时统一模块。
//
// 职责(把“WiFi 不写死、可自主配网、能校时”收拢到一处):
//   1. NVS 持久化 WiFi 凭证,供任意用户自己的网络使用(不再强制唯一);
//   2. STA 连接 + SNTP 校时(用户连上网后实时校准日历日期);
//   3. 软AP + 内嵌 HTTP 配网页(设备开热点,手机访问填入 WiFi),作为兜底;
//   4. BLE GATT 配网服务(手机/NFC 客户端经蓝牙下发 WiFi 与当前时间)。
//
// 与播放页零耦合:只通过一个可注册的“日期校准回调”通知上层(日历)刷新日期,
// 因此本模块可在任意时刻(配网中 / 播放中)被调用,不持有 UI。
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// 当前系统状态(供 Setup 页展示)。
typedef enum {
    NET_IDLE = 0,        // 未初始化
    NET_BLE,             // BLE 配网服务运行中
    NET_STA_CONNECTING,  // 正在用已存凭证连接 WiFi
    NET_STA_ONLINE,      // 已连 WiFi(SNTP 校时中)
    NET_STA_FAILED,      // 连接失败或未保存凭证
    NET_AP,              // 软AP 配网运行中(HTTP 页面)
    NET_APWAIT_SWITCH,   // 软AP 已启动,等待客户端连接
} net_state_t;

const char *net_state_str(net_state_t s);

// 当前联网/配网状态(供 Setup 页展示)。
net_state_t net_state_get(void);

// 日期校准回调:配网/校时成功后,把“今天”的本地日期告知上层(日历)。
// year 1000..9999,month 1..12,day 1..31。
typedef void (*net_cal_cb_t)(uint16_t year, uint8_t month, uint8_t day);
// 注册/清除日期校准回调(通常由会话页 enter/exit 时调用)。
void net_cal_set_cb(net_cal_cb_t cb);

// NVS 中是否已存有 WiFi 凭证。
bool net_have_creds(void);

// 保存 WiFi 凭证到 NVS(串口/网页/BLE 均可调用),保存后自动发起连接。
esp_err_t net_sta_set_creds(const char *ssid, const char *pass);

// 初始化并(若已有凭证)尝试 STA 连接。可重复调用;连接目标是“用户自己的 WiFi”。
esp_err_t net_sta_start(void);

// 是否已连接上 WiFi 并拿到 IP。
bool net_sta_is_connected(void);

// 软AP + HTTP 配网页:启动后设备成为热点,手机访问 192.168.4.1 即可填 WiFi。
esp_err_t net_softap_start(void);
void net_softap_stop(void);

// BLE 配网服务:NimBLE 外设 + 自定义 GATT,接收 WiFi 凭证与 Unix 时间戳。
// 注意:NimBLE 全机单实例,与 BLE 演示页互斥,二者不应同时运行。
esp_err_t net_ble_start(void);
void net_ble_stop(void);

// 停止全部联网/配网资源(Setup 页退出时调用,释放 WiFi/NimBLE/HTTP 以省电)。
void net_stop_all(void);

// 时区偏移(分钟,东八区为 +480),用于把 UTC 换算成“今天”的本地日期。
// 返回 0 表示采用默认东八区。
int32_t net_tz_offset_min(void);