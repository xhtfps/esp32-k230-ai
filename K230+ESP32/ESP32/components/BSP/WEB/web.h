/**
 * @file web.h
 * @brief Web 服务器模块接口
 *
 * 提供启动嵌入式 HTTP 服务的入口函数，服务启动后会注册
 * 主页、视觉状态查询、大模型分析及 Wi‑Fi 配网等路由。
 */
#pragma once

#include "esp_err.h"

/**
 * @brief 启动 HTTP 服务器并注册所有 URI 路由
 *
 * 配置并启动一个轻量级 HTTP 服务，监听默认端口 80。
 * 服务器启动后，可通过浏览器访问设备 IP 来获取主页面、
 * 查询视觉结果、触发大模型分析或进行 Wi‑Fi 配网。
 *
 * @return ESP_OK 表示启动成功，其他值表示错误。
 */
esp_err_t web_start(void);
