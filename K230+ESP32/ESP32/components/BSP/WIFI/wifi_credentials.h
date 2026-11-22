#pragma once
// 防止头文件被重复包含

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// Wi-Fi 凭证字符串的最大允许长度
#define WIFI_CREDENTIALS_SSID_MAX_LEN 32U      // SSID 最大长度（不含结束符）
#define WIFI_CREDENTIALS_PASSWORD_MAX_LEN 63U  // 密码最大长度（不含结束符）

/**
 * Wi-Fi 凭证结构体
 * 存储用于连接无线网络的 SSID 和密码。
 * 字符数组大小比最大长度多 1 字节，以确保容纳字符串结束符 '\0'。
 */
typedef struct {
    char ssid[WIFI_CREDENTIALS_SSID_MAX_LEN + 1U];          // Wi-Fi 网络名称
    char password[WIFI_CREDENTIALS_PASSWORD_MAX_LEN + 1U];   // Wi-Fi 密码
} wifi_credentials_t;

/**
 * 计算给定字符串的长度，但不超过 limit。
 * 用于安全地获取以 '\0' 结尾的字符串长度，同时防止越界访问。
 *
 * @param value 目标字符串
 * @param limit 最大检查长度（含结束符在内）
 * @return 字符串实际长度（不包含 '\0'），不会超过 limit-1
 */
static inline size_t wifi_credential_length(const char *value, size_t limit)
{
    size_t length = 0;
    while (length < limit && value[length] != '\0') {
        length++;
    }
    return length;
}

/**
 * 验证 Wi-Fi 凭证是否有效
 * 检查条件：
 * - 指针非空
 * - 字符串非空（首字符不是 '\0'）
 * - SSID 和密码的长度均在规定的最大限制之内
 *
 * @param ssid     SSID 字符串
 * @param password 密码字符串
 * @return true 如果凭证有效，否则 false
 */
static inline bool wifi_credentials_are_valid(const char *ssid, const char *password)
{
    return ssid != NULL && password != NULL && ssid[0] != '\0' &&
           wifi_credential_length(ssid, WIFI_CREDENTIALS_SSID_MAX_LEN + 1U) <= WIFI_CREDENTIALS_SSID_MAX_LEN &&
           wifi_credential_length(password, WIFI_CREDENTIALS_PASSWORD_MAX_LEN + 1U) <= WIFI_CREDENTIALS_PASSWORD_MAX_LEN;
}
