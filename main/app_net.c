// main/app_net.c —— 联网 / 配网 / 校时统一实现,见 app_net.h 职责说明。
//
// 设计要点:
//   1. WiFi 凭证存 NVS(命名空间 "net"),天然支持"用户自己的网络",不写死;
//   2. 软AP 与 STA 二选一(单 WiFi 子系统),切换前先完整拆掉旧的,避免模式冲突;
//   3. SNTP 只在 STA 拿到 IP 后启动;校时后经回调把"今天"本地日期上报给日历;
//   4. BLE 配网走 NimBLE 外设 + 自定义 GATT(与 BLE 演示页互斥,全机单实例);
//   5. 全部资源可一次性释放(Setup 页退出),保证静音优先下的续航。
#include "app_net.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <string.h>
#include <time.h>

static const char *TAG = "app_net";

// ---------- 常量 ----------
#define NET_NVS_NS     "net"
#define NET_KEY_SSID   "ssid"
#define NET_KEY_PASS   "pass"
#define NET_KEY_TZ     "tz"

#define NET_AP_SSID    "FoloToy-Calendar"
#define NET_AP_PASS    "12345678"
#define NET_AP_MAXCONN 4
#define NET_MAX_SSID   32
#define NET_MAX_PASS   64

#define NET_TZ_DEFAULT (8 * 60)    // 东八区(分钟);可在 NVS 改

#define NET_DEV_NAME   "FoloPassport"

// BLE 配网服务:GATT 自定义 16 位 UUID。
#define NET_SVC_UUID 0xFFF0        // 配网服务
#define NET_CHR_UUID 0xFFF1        // 写入通道(每写一条命令)
#define NET_CMD_MAX   96           // 单条命令/BLE 单次写入上限

// 软AP HTTP 服务端口(DNS 不做 captive portal,页面提示手动访问 192.168.4.1)。
#define NET_HTTPD_PORT 80

// ---------- 状态与回调 ----------
static net_state_t     s_state = NET_IDLE;
static net_cal_cb_t    s_cal_cb;
static int32_t         s_tz_min;

// ---------- WiFi 资源句柄 ----------
static esp_netif_t    *s_sta_netif;
static esp_netif_t    *s_ap_netif;
static esp_event_handler_instance_t s_evt_wifi;      // WIFI_EVENT
static esp_event_handler_instance_t s_evt_ip;        // IP_EVENT
static bool s_wifi_inited;
static bool s_wifi_started;
static httpd_handle_t  s_httpd;

// ---------- BLE 资源句柄 ----------
static SemaphoreHandle_t s_nimble_stopped;
static bool s_nimble_inited;
static bool s_nimble_advertising;
static uint8_t s_nimble_addr_type;
static bool s_ble_want_start;
// BLE 已接收的待提交配置(手机/NFC 客户端经 GATT 下发)。
static char     s_pend_ssid[NET_MAX_SSID];
static char     s_pend_pass[NET_MAX_PASS];
static int64_t  s_pend_unix;

// ---------- 时区 ----------
static int32_t tz_offset_min(void)
{
    return s_tz_min ? s_tz_min : NET_TZ_DEFAULT;
}

// 把 UTC Unix 秒换算成本地日期,并上报给日历。
static void publish_time(int64_t unix_sec)
{
    time_t t = (time_t)(unix_sec + (int64_t)tz_offset_min() * 60);
    struct tm tm;
    if (gmtime_r(&t, &tm) == NULL) return;
    if (s_cal_cb) {
        s_cal_cb((uint16_t)(tm.tm_year + 1900),
                 (uint8_t)(tm.tm_mon + 1),
                 (uint8_t)tm.tm_mday);
    }
    ESP_LOGI(TAG, "校时: %04u-%02u-%02u (tz%+3d)",
             (unsigned)(tm.tm_year + 1900), (unsigned)(tm.tm_mon + 1),
             (unsigned)tm.tm_mday, (int)tz_offset_min() / 60);
}

// ---------- 状态 ----------
net_state_t net_state_get(void) { return s_state; }

const char *net_state_str(net_state_t s)
{
    switch (s) {
    case NET_IDLE:         return "Idle";
    case NET_BLE:          return "BLE ready";
    case NET_STA_CONNECTING: return "WiFi connecting";
    case NET_STA_ONLINE:   return "WiFi online";
    case NET_STA_FAILED:   return "WiFi no route";
    case NET_AP:           return "SoftAP up";
    case NET_APWAIT_SWITCH: return "SoftAP: switch phone";
    default:               return "?";
    }
}

void net_cal_set_cb(net_cal_cb_t cb)
{
    s_cal_cb = cb;
}

// ---------- NVS ----------
static void nvs_ensure(void)
{
    nvs_flash_init();   // 重复调用失败无碍,nvs_open 仍可用
}

bool net_have_creds(void)
{
    nvs_handle_t h;
    char buf[NET_MAX_SSID];
    nvs_ensure();
    if (nvs_open(NET_NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t err = nvs_get_str(h, NET_KEY_SSID, buf, &(size_t){ sizeof(buf) });
    nvs_close(h);
    return err == ESP_OK && buf[0] != '\0';
}

static bool creds_load(char *ssid, size_t ssid_n, char *pass, size_t pass_n)
{
    nvs_handle_t h;
    nvs_ensure();
    if (nvs_open(NET_NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = (nvs_get_str(h, NET_KEY_SSID, ssid, &ssid_n) == ESP_OK &&
               nvs_get_str(h, NET_KEY_PASS, pass, &pass_n) == ESP_OK);
    nvs_close(h);
    return ok;
}

static esp_err_t creds_store(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NET_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return ESP_ERR_NVS_NOT_FOUND;
    esp_err_t err = nvs_set_str(h, NET_KEY_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(h, NET_KEY_PASS, pass);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static void tz_load(void)
{
    nvs_handle_t h;
    nvs_ensure();
    s_tz_min = NET_TZ_DEFAULT;
    if (nvs_open(NET_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, NET_KEY_TZ, &s_tz_min);
        nvs_close(h);
    }
}
int32_t net_tz_offset_min(void) { return tz_offset_min(); }

// ---------- SNTP 校时 ----------
static void sntp_time_cb(struct timeval *tv)
{
    if (tv && tv->tv_sec > 0) {
        publish_time((int64_t)tv->tv_sec);
        esp_sntp_stop();   // 校准一次即可,避免长期轮询耗电
    }
}

static esp_err_t sntp_start(void)
{
    if (esp_sntp_enabled()) return ESP_OK;
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(sntp_time_cb);
    esp_sntp_init();
    return ESP_OK;
}

// ---------- WiFi ----------
static void wifi_teardown(void)
{
    if (s_httpd) { httpd_stop(s_httpd); s_httpd = NULL; }
    if (s_evt_wifi)  { esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_evt_wifi);  s_evt_wifi = NULL; }
    if (s_evt_ip)    { esp_event_handler_instance_unregister(IP_EVENT,   ESP_EVENT_ANY_ID, s_evt_ip);    s_evt_ip = NULL; }
    if (s_wifi_started) { esp_wifi_stop(); s_wifi_started = false; }
    if (s_wifi_inited)  { esp_wifi_deinit(); s_wifi_inited = false; }
    if (s_sta_netif) { esp_netif_destroy_default_wifi(s_sta_netif); s_sta_netif = NULL; }
    if (s_ap_netif)  { esp_netif_destroy_default_wifi(s_ap_netif);  s_ap_netif = NULL; }
    s_state = NET_IDLE;
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        char ssid[NET_MAX_SSID], pass[NET_MAX_PASS];
        if (creds_load(ssid, sizeof(ssid), pass, sizeof(pass))) {
            wifi_config_t cfg = { 0 };
            strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
            strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
            cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            if (esp_wifi_set_config(WIFI_IF_STA, &cfg) == ESP_OK)
                esp_wifi_connect();
        } else {
            s_state = NET_STA_FAILED;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *g = (ip_event_got_ip_t *)data;
        s_state = NET_STA_ONLINE;
        ESP_LOGI(TAG, "已连 WiFi,IP=" IPSTR, IP2STR(&g->ip_info.ip));
        sntp_start();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        // 不无限重连:配网一次成功即可,失败留给 Setup 页人工处理(省电)。
        s_state = NET_STA_FAILED;
    }
}

esp_err_t net_sta_set_creds(const char *ssid, const char *pass)
{
    if (!ssid || !*ssid) return ESP_ERR_INVALID_ARG;
    if (pass == NULL) pass = "";
    esp_err_t err = creds_store(ssid, pass);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "已保存 WiFi 凭证: %s", ssid);
    // 若当前已是 STA 模式则立即重连;否则等下次 STA 启动时生效。
    if (s_wifi_started && esp_wifi_get_mode(NULL) == ESP_OK) {
        wifi_mode_t m;
        if (esp_wifi_get_mode(&m) == ESP_OK && m == WIFI_MODE_STA) {
            wifi_config_t cfg = { 0 };
            strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
            strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
            cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            esp_wifi_set_config(WIFI_IF_STA, &cfg);
            s_state = NET_STA_CONNECTING;
            return esp_wifi_connect();
        }
    }
    return ESP_OK;
}

esp_err_t net_sta_start(void)
{
    esp_err_t err;
    if (s_state == NET_STA_CONNECTING || s_state == NET_STA_ONLINE) return ESP_OK;
    wifi_teardown();
    tz_load();

    // 已存在 STA netif 则跳过重开(示例沿用 demo_radio 的初始化约定)。
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) return ESP_ERR_NO_MEM;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) { wifi_teardown(); return err; }
    s_wifi_inited = true;

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL, &s_evt_wifi);
    esp_event_handler_instance_register(IP_EVENT,   ESP_EVENT_ANY_ID, event_handler, NULL, &s_evt_ip);

    esp_wifi_set_storage(WIFI_STORAGE_RAM);        // 凭证由本模块 NVS 自管
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    s_wifi_started = true;
    s_state = net_have_creds() ? NET_STA_CONNECTING : NET_STA_FAILED;
    return ESP_OK;
}

bool net_sta_is_connected(void) { return s_state == NET_STA_ONLINE; }

// ---------- 软AP + HTTP 配网页 ----------
static const char *softap_page =
    "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
    "content=\"width=device-width,initial-scale=1\"><title>FoloToy Calendar - 配网</title>"
    "<style>body{font-family:system-ui;max-width:420px;margin:24px auto;padding:0 16px}"
    "h1{font-size:18px}label{display:block;font-size:13px;margin:12px 0 4px}input{width:100%;"
    "box-sizing:border-box;padding:10px;font-size:16px;border:1px solid #ccc;border-radius:6px}"
    "button{width:100%;padding:12px;margin-top:18px;font-size:16px;background:#2563eb;color:#fff;"
    "border:0;border-radius:6px}</style></head>"
    "<body><h1>FoloToy 日历 配网</h1>"
    "<form method=\"post\" action=\"/save\">"
    "<label>WiFi 名称 (SSID)</label><input name=\"ssid\" required autocomplete=\"off\">"
    "<label>WiFi 密码</label><input name=\"pass\" type=\"text\" autocomplete=\"off\">"
    "<button type=\"submit\">保存并连接</button>"
    "</form><p style=\"color:#888;font-size:12px\">配置只在设备本地保存。</p></body></html>";

static esp_err_t http_save_handler(httpd_req_t *req)
{
    char buf[256] = { 0 };
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
    buf[ret] = '\0';

    char ssid[NET_MAX_SSID] = { 0 }, pass[NET_MAX_PASS] = { 0 };
    size_t sn = sizeof(ssid), pn = sizeof(pass);
    httpd_query_key_value(buf, "ssid", ssid, sn);
    httpd_query_key_value(buf, "pass", pass, pn);

    char resp[128];
    if (net_sta_set_creds(ssid, pass) == ESP_OK) {
        snprintf(resp, sizeof(resp),
                 "<!doctype html><meta charset=\"utf-8\"><body><h3>已保存 %s,"
                 "正在尝试连接 WiFi...</h3></body>", ssid);
    } else {
        snprintf(resp, sizeof(resp), "<!doctype html><meta charset=\"utf-8\">"
                 "<body><h3>保存失败,请重试</h3></body>");
    }
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t http_index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, softap_page, HTTPD_RESP_USE_STRLEN);
}

esp_err_t net_softap_start(void)
{
    esp_err_t err;
    wifi_teardown();
    tz_load();

    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) return ESP_ERR_NO_MEM;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) { wifi_teardown(); return err; }
    s_wifi_inited = true;

    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    wifi_config_t ap = { 0 };
    strncpy((char *)ap.ap.ssid, NET_AP_SSID, sizeof(ap.ap.ssid) - 1);
    strncpy((char *)ap.ap.password, NET_AP_PASS, sizeof(ap.ap.password) - 1);
    ap.ap.ssid_len = (uint8_t)strlen(NET_AP_SSID);
    ap.ap.max_connection = NET_AP_MAXCONN;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    esp_wifi_start();
    s_wifi_started = true;

    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    hcfg.uri_match_fn = NULL;
    hcfg.server_port = NET_HTTPD_PORT;
    hcfg.stack_size = 4096;
    hcfg.lru_purge_enable = true;
    if (httpd_start(&s_httpd, &hcfg) == ESP_OK) {
        httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET,  .handler = http_index_handler };
        httpd_uri_t save_uri  = { .uri = "/save", .method = HTTP_POST, .handler = http_save_handler };
        httpd_register_uri_handler(s_httpd, &index_uri);
        httpd_register_uri_handler(s_httpd, &save_uri);
    }

    s_state = NET_AP;
    ESP_LOGI(TAG, "软AP 已启动: %s / %s, 配网页 http://192.168.4.1", NET_AP_SSID, NET_AP_PASS);
    return ESP_OK;
}

void net_softap_stop(void) { if (s_state == NET_AP) wifi_teardown(); }

// ---------- BLE 配网服务 ----------
static int prov_gap_event(struct ble_gap_event *event, void *arg);

static int prov_write_access(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    struct os_mbuf *om = ctxt->om;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;

    uint16_t len = OS_MBUF_PKTLEN(om);
    if (len >= NET_CMD_MAX) len = NET_CMD_MAX - 1;
    char buf[NET_CMD_MAX];
    uint16_t got = 0;
    while (om != NULL) {
        if (got + om->om_len > len) { om->om_len = len - got; }
        memcpy(buf + got, om->om_data, om->om_len);
        got += om->om_len;
        om = SLIST_NEXT(om, om_next);
    }
    buf[got] = '\0';

    if      (strncmp(buf, "W:", 2) == 0) strncpy(s_pend_ssid, buf + 2, sizeof(s_pend_ssid) - 1);
    else if (strncmp(buf, "P:", 2) == 0) strncpy(s_pend_pass, buf + 2, sizeof(s_pend_pass) - 1);
    else if (strncmp(buf, "T:", 2) == 0) s_pend_unix = (int64_t)atoll(buf + 2);
    else if (buf[0] == 'C') {
        if (s_pend_ssid[0]) {
            net_sta_set_creds(s_pend_ssid, s_pend_pass);
            ESP_LOGI(TAG, "BLE 下发并保存 WiFi: %s", s_pend_ssid);
        }
        if (s_pend_unix > 0) publish_time(s_pend_unix);   // NFC/手机校时主通道
        memset(s_pend_ssid, 0, sizeof(s_pend_ssid));
        memset(s_pend_pass, 0, sizeof(s_pend_pass));
        s_pend_unix = 0;
    }
    return 0;
}

static const struct ble_gatt_svc_def prov_svcs[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY,
      .uuid = &((const ble_uuid16_t)BLE_UUID16_INIT(NET_SVC_UUID)).u,
      .characteristics = (struct ble_gatt_chr_def[]){
          { .uuid = &((const ble_uuid16_t)BLE_UUID16_INIT(NET_CHR_UUID)).u,
            .access_cb = prov_write_access,
            .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP },
          { 0 },
      },
    },
    { 0 },
};

static int prov_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        s_nimble_advertising = false;
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_nimble_advertising = false;
        if (s_ble_want_start) {
            ble_gap_adv_start(s_nimble_addr_type, NULL, BLE_HS_FOREVER,
                              &(struct ble_gap_adv_params){
                                  .conn_mode = BLE_GAP_CONN_MODE_UND,
                                  .disc_mode = BLE_GAP_DISC_MODE_GEN,
                              }, prov_gap_event, NULL);
            s_nimble_advertising = true;
        }
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_ble_want_start) {
            ble_gap_adv_start(s_nimble_addr_type, NULL, BLE_HS_FOREVER,
                              &(struct ble_gap_adv_params){
                                  .conn_mode = BLE_GAP_CONN_MODE_UND,
                                  .disc_mode = BLE_GAP_DISC_MODE_GEN,
                              }, prov_gap_event, NULL);
            s_nimble_advertising = true;
        }
        break;
    default:
        break;
    }
    return 0;
}

static void prov_on_reset(int reason) { (void)reason; }
static void prov_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_nimble_addr_type);
    if (rc == 0 && s_ble_want_start) {
        rc = ble_gap_adv_start(s_nimble_addr_type, NULL, BLE_HS_FOREVER,
                               &(struct ble_gap_adv_params){
                                   .conn_mode = BLE_GAP_CONN_MODE_UND,
                                   .disc_mode = BLE_GAP_DISC_MODE_GEN,
                               }, prov_gap_event, NULL);
        s_nimble_advertising = (rc == 0);
    }
}

static void prov_host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    if (s_nimble_stopped) xSemaphoreGive(s_nimble_stopped);
    nimble_port_freertos_deinit();
}

esp_err_t net_ble_start(void)
{
    if (s_nimble_inited) { s_ble_want_start = true; return ESP_OK; }

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK && err != ESP_ERR_NVS_NO_FREE_PAGES && err != ESP_ERR_NVS_NEW_VERSION_FOUND)
        return err;

    err = nimble_port_init();
    if (err != ESP_OK) return err;
    s_nimble_inited = true;
    s_nimble_stopped = xSemaphoreCreateBinary();
    if (!s_nimble_stopped) { nimble_port_deinit(); s_nimble_inited = false; return ESP_ERR_NO_MEM; }

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(NET_DEV_NAME);
    ble_gatts_count_cfg(prov_svcs);
    ble_gatts_add_svcs(prov_svcs);

    ble_hs_cfg.reset_cb = prov_on_reset;
    ble_hs_cfg.sync_cb  = prov_on_sync;
    s_ble_want_start = true;
    nimble_port_freertos_init(prov_host_task);

    s_state = NET_BLE;
    return ESP_OK;
}

void net_ble_stop(void)
{
    s_ble_want_start = false;
    if (!s_nimble_inited) return;
    ble_gap_adv_stop();
    int rc = nimble_port_stop();
    if (rc == 0 && s_nimble_stopped) xSemaphoreTake(s_nimble_stopped, portMAX_DELAY);
    if (rc == 0) { nimble_port_deinit(); s_nimble_inited = false; }
    if (!s_nimble_inited && s_nimble_stopped) { vSemaphoreDelete(s_nimble_stopped); s_nimble_stopped = NULL; }
    s_nimble_advertising = false;
    if (s_state == NET_BLE) s_state = NET_IDLE;
}

void net_stop_all(void)
{
    net_ble_stop();
    wifi_teardown();
    s_cal_cb = NULL;
}