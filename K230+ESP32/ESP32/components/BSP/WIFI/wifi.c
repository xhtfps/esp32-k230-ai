/**
 * @file wifi.c
 * @brief ESP32 Wi-Fi 管理模块
 *
 * 实现 STA 模式连接、配网 SoftAP 模式、凭据持久化存储（NVS）、
 * 自动重连及 LED 状态指示等功能。
 */

#include "wifi.h"

#include <stdbool.h>
#include <string.h>

#include "esp_check.h"          // ESP-IDF 错误检查宏
#include "esp_event.h"         // 事件循环
#include "esp_log.h"           // 日志
#include "esp_netif.h"         // 网络接口抽象
#include "esp_timer.h"         // 软件定时器
#include "esp_wifi.h"          // Wi-Fi 驱动
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"   // FreeRTOS 信号量
#include "nvs.h"               // 非易失性存储
#include "nvs_flash.h"         // NVS 闪存初始化

#include "led.h"
#include "wifi_config.h"       // 编译期默认 Wi-Fi 配置
#include "wifi_credentials.h"  // 凭据结构与校验
#include "wifi_softap_profile.h" // SoftAP 参数配置

// ---------- 本地常量定义 ----------
#define WIFI_NVS_NAMESPACE "wifi_cfg"         // NVS 命名空间
#define WIFI_NVS_CREDENTIALS_KEY "credentials" // 凭据存储键名

#define WIFI_PROVISIONING_SSID     "OV2640-Setup"   // 配网热点 SSID
#define WIFI_PROVISIONING_PASSWORD "66666666"       // 配网热点密码

#define WIFI_RETRY_DELAY_US (5 * 1000 * 1000)       // 重连延迟 5 秒（微秒）

/**
 * @brief 运行时 Wi-Fi 工作模式
 */
typedef enum {
    WIFI_RUNTIME_MODE_STA,          // 客户端模式，连接路由器
    WIFI_RUNTIME_MODE_PROVISIONING, // 配网模式，设备作为 SoftAP
} wifi_runtime_mode_t;

static const char *TAG = "WIFI";    // 日志标签

// ---------- 模块内部状态 ----------
static bool s_initialized;           // 模块是否已初始化
static bool s_wifi_started;          // Wi-Fi 底层是否已启动 (esp_wifi_start)
static bool s_transitioning;         // 是否正在进行模式切换（停止/启动过渡中）
static uint8_t s_connect_failures;   // 连续连接失败计数
static esp_timer_handle_t s_retry_timer; // 重连定时器句柄
static SemaphoreHandle_t s_mode_lock;    // 模式切换互斥锁（保护临界区）
static wifi_runtime_mode_t s_runtime_mode = WIFI_RUNTIME_MODE_STA; // 当前工作模式
static esp_netif_t *s_ap_netif;      // SoftAP 网络接口指针

// ---------- 前向声明 ----------
static void wifi_stop_retry_timer(void);
static void wifi_load_default_credentials(wifi_credentials_t *credentials);
static esp_err_t wifi_load_credentials(wifi_credentials_t *credentials);
static esp_err_t wifi_stop_current_mode(void);
static esp_err_t wifi_start_station_locked(void);
static void wifi_retry_callback(void *arg);
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);

/**
 * @brief 停止重连定时器（如果正在运行）
 */
static void wifi_stop_retry_timer(void)
{
    if (s_retry_timer != NULL && esp_timer_is_active(s_retry_timer)) {
        esp_timer_stop(s_retry_timer);
    }
}

/**
 * @brief 加载编译期默认 Wi-Fi 凭据（兜底方案）
 */
static void wifi_load_default_credentials(wifi_credentials_t *credentials)
{
    strlcpy(credentials->ssid, WIFI_SSID, sizeof(credentials->ssid));
    strlcpy(credentials->password, WIFI_PASSWORD, sizeof(credentials->password));
    ESP_LOGI(TAG, "未找到已保存凭据，使用编译期默认网络");
}

/**
 * @brief 从 NVS 中加载已保存的 Wi-Fi 凭据
 *
 * 若 NVS 中不存在或无效，则回退到编译期默认凭据。
 * 同时会过滤掉意外保存的配网热点 SSID，防止 STA 尝试连接自己的 SoftAP。
 *
 * @param[out] credentials 存放读取到的凭据
 * @return ESP_OK 成功（可能已回退为默认值），其他值表示错误
 */
static esp_err_t wifi_load_credentials(wifi_credentials_t *credentials)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        wifi_load_default_credentials(credentials);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t credentials_size = sizeof(*credentials);
    err = nvs_get_blob(nvs, WIFI_NVS_CREDENTIALS_KEY, credentials, &credentials_size);
    nvs_close(nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        wifi_load_default_credentials(credentials);
        return ESP_OK;
    }
    if (err != ESP_OK || credentials_size != sizeof(*credentials) ||
        !wifi_credentials_are_valid(credentials->ssid, credentials->password)) {
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }

    /*
     * OV2640-Setup 是本设备临时 SoftAP 的 SSID，不是上游路由网络。
     * 如果之前的配网流程意外将其保存，则回退到编译期 STA 凭据，
     * 以避免 STA 尝试连接自己的热点。
     */
    if (strcmp(credentials->ssid, WIFI_PROVISIONING_SSID) == 0) {
        ESP_LOGW(TAG, "NVS 中保存的是配网热点 SSID，STA 改用编译期默认网络");
        wifi_load_default_credentials(credentials);
    } else {
        ESP_LOGI(TAG, "使用 NVS 中保存的 STA 网络：SSID=%s", credentials->ssid);
    }
    return ESP_OK;
}

/**
 * @brief 持久化存储 Wi-Fi 凭据到 NVS
 *
 * @param ssid      Wi-Fi SSID
 * @param password  Wi-Fi 密码
 * @return ESP_OK 成功，否则失败
 */
esp_err_t wifi_save_credentials(const char *ssid, const char *password)
{
    if (!wifi_credentials_are_valid(ssid, password)) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_credentials_t credentials = {0};
    strlcpy(credentials.ssid, ssid, sizeof(credentials.ssid));
    strlcpy(credentials.password, password, sizeof(credentials.password));

    // 获取模式锁，防止写操作与模式切换竞争
    if (s_mode_lock == NULL || xSemaphoreTake(s_mode_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, WIFI_NVS_CREDENTIALS_KEY, &credentials, sizeof(credentials));
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    xSemaphoreGive(s_mode_lock);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "保存 Wi-Fi 凭据失败：%s", esp_err_to_name(err));
    }
    return err;
}

/**
 * @brief 安全停止当前 Wi-Fi 模式（STA 或 AP）
 *
 * 同时会停止重连定时器。
 * @return ESP_OK 成功停止或未启动，否则返回错误
 */
static esp_err_t wifi_stop_current_mode(void)
{
    wifi_stop_retry_timer();
    if (!s_wifi_started) {
        return ESP_OK;
    }

    esp_err_t err = esp_wifi_stop();
    if (err == ESP_OK || err == ESP_ERR_WIFI_NOT_STARTED) {
        s_wifi_started = false;
        return ESP_OK;
    }
    return err;
}

/**
 * @brief 在持有模式锁的情况下启动 STA 模式
 *
 * 负责加载凭据、配置并启动 Wi-Fi 连接。
 * @return ESP_OK 成功，否则失败
 */
static esp_err_t wifi_start_station_locked(void)
{
    wifi_credentials_t credentials = {0};
    esp_err_t err = wifi_load_credentials(&credentials);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "读取 Wi-Fi 凭据失败：%s", esp_err_to_name(err));
        return err;
    }

    s_transitioning = true;
    s_runtime_mode = WIFI_RUNTIME_MODE_STA;
    err = wifi_stop_current_mode();
    if (err != ESP_OK) {
        s_transitioning = false;
        return err;
    }

    // 填充 Wi-Fi STA 配置
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, credentials.ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, credentials.password, sizeof(config.sta.password));
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;   // 最低认证要求 WPA2
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    ESP_LOGI(TAG, "准备连接 Wi-Fi：SSID=%s，认证最低要求=WPA2，设备仅支持 2.4 GHz",
             (char *)config.sta.ssid);

    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK ||
        (err = esp_wifi_set_config(WIFI_IF_STA, &config)) != ESP_OK) {
        s_transitioning = false;
        return err;
    }

    s_wifi_started = true;
    err = esp_wifi_start();
    if (err != ESP_OK) {
        s_wifi_started = false;
        s_transitioning = false;
        return err;
    }
    s_connect_failures = 0;   // 重置失败计数
    led_set_wifi_connecting(); // LED 指示“连接中”
    ESP_LOGI(TAG, "正在连接 STA 网络");
    return ESP_OK;
}

/**
 * @brief 公开接口：切换到 STA 模式并连接 Wi-Fi
 *
 * @return ESP_OK 成功，否则失败
 */
esp_err_t wifi_start_station(void)
{
    if (!s_initialized || xSemaphoreTake(s_mode_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = wifi_start_station_locked();
    xSemaphoreGive(s_mode_lock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "切换 STA 模式失败：%s", esp_err_to_name(err));
    }
    return err;
}

/**
 * @brief 公开接口：启动配网 SoftAP 模式
 *
 * 设备创建一个 Wi-Fi 热点，供手机等终端连接并进行网络配置。
 * @return ESP_OK 成功，否则失败
 */
esp_err_t wifi_start_provisioning(void)
{
    if (!s_initialized || xSemaphoreTake(s_mode_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    s_transitioning = true;
    s_runtime_mode = WIFI_RUNTIME_MODE_PROVISIONING;
    esp_err_t err = wifi_stop_current_mode();
    wifi_config_t config = {0};
    if (err == ESP_OK) {
        strlcpy((char *)config.ap.ssid, WIFI_PROVISIONING_SSID, sizeof(config.ap.ssid));
        strlcpy((char *)config.ap.password, WIFI_PROVISIONING_PASSWORD, sizeof(config.ap.password));
        config.ap.ssid_len = strlen(WIFI_PROVISIONING_SSID);
        config.ap.channel = WIFI_PROVISIONING_CHANNEL;
        config.ap.max_connection = 4;
        config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        config.ap.ssid_hidden = WIFI_PROVISIONING_SSID_HIDDEN;
        config.ap.beacon_interval = WIFI_PROVISIONING_BEACON_INTERVAL;
        config.ap.pmf_cfg.capable = true;
        config.ap.pmf_cfg.required = false;
        err = esp_wifi_set_mode(WIFI_MODE_AP);
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &config);
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROVISIONING_PROTOCOLS);
    }
    if (err == ESP_OK) {
        s_wifi_started = true;
        s_transitioning = false;
        err = esp_wifi_start();
    }
    if (err == ESP_OK) {
        s_connect_failures = 0;
        led_set_wifi_provisioning(); // LED 指示“配网模式”
        ESP_LOGI(TAG, "配网热点启动请求已完成，等待 AP_START 事件确认");
    } else {
        s_wifi_started = false;
        s_transitioning = false;
        ESP_LOGE(TAG, "启动配网热点失败：%s", esp_err_to_name(err));
    }
    xSemaphoreGive(s_mode_lock);
    return err;
}

/**
 * @brief 查询当前是否处于配网模式
 */
bool wifi_is_provisioning(void)
{
    return s_runtime_mode == WIFI_RUNTIME_MODE_PROVISIONING;
}

/**
 * @brief 重连定时器回调：尝试重新连接 Wi-Fi
 */
static void wifi_retry_callback(void *arg)
{
    (void)arg;
    if (s_runtime_mode == WIFI_RUNTIME_MODE_STA && s_wifi_started && !s_transitioning) {
        led_set_wifi_connecting();
        esp_wifi_connect();
    }
}

/**
 * @brief Wi-Fi 及 IP 事件统一处理函数
 *
 * 监听 AP 启动、STA 连接/断开、获取 IP 等关键事件，
 * 并据此更新状态、LED 指示、重连逻辑等。
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    // 配网热点成功启动事件
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START &&
               s_runtime_mode == WIFI_RUNTIME_MODE_PROVISIONING && s_wifi_started) {
        wifi_config_t config = {0};
        esp_netif_ip_info_t ip_info = {0};
        esp_wifi_get_config(WIFI_IF_AP, &config);
        if (s_ap_netif != NULL) {
            esp_netif_get_ip_info(s_ap_netif, &ip_info);
        }
        ESP_LOGI(TAG, "配网热点已就绪：SSID=%s，信道=%u，隐藏=%u，IP=" IPSTR,
                 config.ap.ssid, config.ap.channel, config.ap.ssid_hidden,
                 IP2STR(&ip_info.ip));
    }
    // STA 启动完成，开始连接
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START &&
        s_runtime_mode == WIFI_RUNTIME_MODE_STA && s_wifi_started) {
        s_transitioning = false;
        led_set_wifi_connecting();
        esp_wifi_connect();
    }
    // STA 断开连接事件
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED &&
               s_runtime_mode == WIFI_RUNTIME_MODE_STA && s_wifi_started && !s_transitioning) {
        const wifi_event_sta_disconnected_t *disconnected = event_data;
        ESP_LOGW(TAG, "STA 断开：reason=%u（常见原因：密码错误、热点不在 2.4 GHz、认证模式不兼容或信号不足）",
                 disconnected == NULL ? 0U : (unsigned int)disconnected->reason);
        s_connect_failures++;
        if (s_connect_failures >= 5) {
            // 连续失败达到阈值，启动定时器延时重试，同时 LED 指示失败
            s_connect_failures = 0;
            led_set_wifi_failed();
            ESP_LOGE(TAG, "连续五次连接失败，五秒后重试");
            esp_timer_start_once(s_retry_timer, WIFI_RETRY_DELAY_US);
        } else {
            led_set_wifi_connecting();
            ESP_LOGW(TAG, "STA 连接失败，正在重连（%u/5）", s_connect_failures);
            esp_wifi_connect(); // 立即重连
        }
    }
    // STA 获取到 IP 地址事件
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP &&
               s_runtime_mode == WIFI_RUNTIME_MODE_STA && s_wifi_started && !s_transitioning) {
        const ip_event_got_ip_t *event = event_data;
        s_connect_failures = 0;  // 清零失败计数
        led_set_wifi_connected();
        ESP_LOGI(TAG, "STA 已获得 IP，浏览器访问：http://" IPSTR "/", IP2STR(&event->ip_info.ip));
    }
}

/**
 * @brief 初始化 Wi-Fi 子系统并启动 STA 连接
 *
 * 执行顺序：NVS 初始化 -> TCP/IP 网络栈 -> Wi-Fi 驱动 -> 事件注册 -> 默认 STA 连接。
 * @return ESP_OK 成功，否则失败
 */
esp_err_t wifi_init_sta(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // 1. 初始化 NVS（存储 Wi-Fi 凭据等）
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS 擦除失败");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "NVS 初始化失败");

    // 2. 初始化 TCP/IP 网络栈和默认网络接口
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "网络接口初始化失败");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "事件循环初始化失败");
    esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    ESP_RETURN_ON_FALSE(s_ap_netif != NULL, ESP_ERR_NO_MEM, TAG, "AP 网络接口创建失败");

    // 3. 创建重连定时器
    const esp_timer_create_args_t retry_timer_args = {
        .callback = wifi_retry_callback,
        .name = "wifi_retry",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&retry_timer_args, &s_retry_timer), TAG, "重连定时器创建失败");

    // 4. 初始化 Wi-Fi 驱动
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "Wi-Fi 初始化失败");

    // 5. 注册事件处理回调
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                             &wifi_event_handler, NULL, NULL),
                        TAG, "Wi-Fi 事件注册失败");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                             &wifi_event_handler, NULL, NULL),
                        TAG, "IP 事件注册失败");

    // 6. 创建模式互斥锁
    s_mode_lock = xSemaphoreCreateMutex();
    if (s_mode_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_initialized = true;

    // 7. 默认以 STA 模式启动
    return wifi_start_station();
}
