/**
 * @file wifi.h
 * @brief Wi-Fi 管理模块公共接口
 *
 * 提供 STA 模式连接、配网 SoftAP 启动、凭据持久化以及模式查询功能。
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief 初始化 Wi-Fi 子系统并以 STA 模式启动连接
 *
 * 该函数会依次初始化 NVS、TCP/IP 协议栈、Wi-Fi 驱动，
 * 注册事件回调，并默认尝试连接已保存或编译期配置的 Wi-Fi 网络。
 *
 * @return ESP_OK 成功，否则返回错误码
 */
esp_err_t wifi_init_sta(void);

/**
 * @brief 启动配网 SoftAP 模式
 *
 * 设备创建一个 Wi-Fi 热点（名称和密码由配置决定），
 * 供手机等客户端连接并完成网络配置。
 *
 * @return ESP_OK 成功，否则返回错误码
 */
esp_err_t wifi_start_provisioning(void);

/**
 * @brief 切换至 STA 模式并尝试连接 Wi-Fi
 *
 * 通常在配网完成后调用，使设备重新连接至已保存的路由器网络。
 *
 * @return ESP_OK 成功，否则返回错误码
 */
esp_err_t wifi_start_station(void);

/**
 * @brief 将 Wi-Fi 凭据持久化保存至 NVS
 *
 * @param ssid     Wi-Fi SSID 字符串
 * @param password Wi-Fi 密码字符串
 * @return ESP_OK 保存成功，否则返回错误码
 */
esp_err_t wifi_save_credentials(const char *ssid, const char *password);

/**
 * @brief 查询当前是否处于配网模式
 *
 * @return true  当前工作在 SoftAP 配网模式
 * @return false 当前工作在 STA 客户端模式
 */
bool wifi_is_provisioning(void);
