/**
 * @file web_provisioning.h
 * @brief Web 配网表单解析工具
 *
 * 提供 application/x-www-form-urlencoded 格式的解码与解析函数，
 * 专门用于从配网页的 POST 请求中提取 Wi-Fi SSID 和密码。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "wifi_credentials.h"

/**
 * @brief 将单个十六进制字符转换为对应的数值
 *
 * 支持 '0'-'9'、'a'-'f'、'A'-'F'。
 *
 * @param value 待转换字符
 * @return 0~15 之间的整数值，若输入非法则返回 -1
 */
static inline int web_hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

/**
 * @brief 解码 URL 编码的表单字段值
 *
 * 将 source 中的百分号编码 (%XX) 和加号 (+) 还原为原始字符，
 * 结果放入 destination 缓冲区并确保以 '\0' 结尾。
 *
 * @param source          原始编码字符串
 * @param source_len      source 的长度（字节）
 * @param destination     输出缓冲区
 * @param destination_size 输出缓冲区大小（字节）
 * @return true  解码成功
 * @return false 缓冲区不足或编码格式错误
 */
static inline bool web_decode_form_value(const char *source, size_t source_len,
                                         char *destination, size_t destination_size)
{
    size_t output_len = 0;
    if (destination_size == 0U) {
        return false;  // 没有空间存放结果
    }

    for (size_t index = 0; index < source_len; index++) {
        char value = source[index];
        if (value == '+') {
            value = ' ';                 // '+' 解码为空格
        } else if (value == '%') {
            if (index + 2U >= source_len) {
                return false;            // 百分号后不足两个字符
            }
            int high = web_hex_value(source[++index]);
            int low = web_hex_value(source[++index]);
            if (high < 0 || low < 0) {
                return false;            // 非法十六进制字符
            }
            value = (char)((high << 4) | low);
        }
        if (output_len + 1U >= destination_size) {
            return false;                // 输出缓冲区不足
        }
        destination[output_len++] = value;
    }
    destination[output_len] = '\0';      // 附加结束符
    return true;
}

/**
 * @brief 解析配网页提交的表单数据
 *
 * 从 application/x-www-form-urlencoded 格式的 POST 正文中
 * 提取 "ssid" 和 "password" 字段，解码并填充到 credentials 结构体中，
 * 同时调用 wifi_credentials_are_valid 进行最终验证。
 *
 * @param body         指向 POST 正文的指针
 * @param credentials  输出，存放解码后的 Wi-Fi 凭据
 * @return true  解析成功且凭据有效
 * @return false 格式错误、缺少字段或凭据不合法
 */
static inline bool web_parse_provisioning_form(const char *body, wifi_credentials_t *credentials)
{
    if (body == NULL || credentials == NULL) {
        return false;
    }

    // 清空输出结构体，以便只检查出现的字段
    memset(credentials, 0, sizeof(*credentials));
    bool has_ssid = false;
    bool has_password = false;
    const char *pair = body;

    // 遍历每一对 key=value（以 & 分隔）
    while (*pair != '\0') {
        const char *end = strchr(pair, '&');
        size_t pair_len = end == NULL ? strlen(pair) : (size_t)(end - pair);
        const char *equals = memchr(pair, '=', pair_len);
        if (equals == NULL) {
            return false;                // 缺少 '='
        }
        size_t key_len = (size_t)(equals - pair);
        const char *value = equals + 1;
        size_t value_len = pair_len - key_len - 1U;

        // 匹配字段名并解码
        if (key_len == 4U && memcmp(pair, "ssid", 4U) == 0) {
            if (has_ssid || !web_decode_form_value(value, value_len, credentials->ssid,
                                                    sizeof(credentials->ssid))) {
                return false;            // 重复字段或解码失败
            }
            has_ssid = true;
        } else if (key_len == 8U && memcmp(pair, "password", 8U) == 0) {
            if (has_password || !web_decode_form_value(value, value_len, credentials->password,
                                                        sizeof(credentials->password))) {
                return false;            // 重复字段或解码失败
            }
            has_password = true;
        }
        // 忽略其他未知字段

        if (end == NULL) {
            break;
        }
        pair = end + 1;                  // 移动到下一个字段
    }

    // 必须同时包含 ssid 和 password，且内容合法
    return has_ssid && has_password && wifi_credentials_are_valid(credentials->ssid, credentials->password);
}
