/**
 * @file wifi_softap_profile.h
 * @brief 配网 SoftAP 模式配置文件
 *
 * 定义了设备作为 Wi-Fi 热点（SoftAP）运行时的物理层和 MAC 层参数，
 * 所有设置均兼容常见 2.4 GHz 手机设备。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 固定使用 2.4 GHz 信道 6，并仅启用 802.11 b/g/n 协议，保证所有手机均可连接 */
#define WIFI_PROVISIONING_CHANNEL         6U    /**< 工作信道（2.4 GHz 第 6 信道） */
#define WIFI_PROVISIONING_PROTOCOLS       0x07U /**< 协议掩码：802.11b | 802.11g | 802.11n */
#define WIFI_PROVISIONING_BEACON_INTERVAL 100U  /**< Beacon 帧发送间隔（毫秒） */
#define WIFI_PROVISIONING_SSID_HIDDEN     false /**< 是否隐藏 SSID，false 表示广播可见 */