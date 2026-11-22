/**
 * @file web.c
 * @brief ESP32 HTTP 服务器模块
 *
 * 实现嵌入式 Web 服务，提供以下接口：
 *   - GET  /            根据工作模式返回主页或配网页
 *   - POST /provision   接收并保存 Wi-Fi 凭据，随后切换至 STA 模式
 *   - GET  /vision      返回 K230 链路状态与最新视觉结果 (JSON)
 *   - GET  /analyze     请求 K230 拍摄 JPEG 并调用大模型分析
 */
#include "web.h"

#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_check.h"          // ESP-IDF 错误检查宏
#include "esp_http_server.h"    // HTTP 服务器 API
#include "esp_log.h"            // 日志
#include "esp_timer.h"          // 高精度定时器

#include "k230_link.h"          // K230 UART 链路状态
#include "llm.h"                // 大模型分析接口
#include "wifi.h"               // Wi-Fi 管理（配网模式查询）
#include "web_provisioning.h"   // 配网表单解析
#include "vision_state.h"       // 视觉结果缓存

static const char *TAG = "WEB";          // 日志标签
static httpd_handle_t s_server;          // HTTP 服务器句柄（全局单实例）

// ---------- 内嵌 HTML 页面 ----------

/**
 * 主页面 (STA 模式)
 * 展示 K230 在线状态、最新人脸识别/视觉结果，并提供“拍照分析”按钮。
 * 通过 fetch 周期性刷新 /vision 接口，分析按钮调用 /analyze。
 */
static const char s_index_html[] =
    "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>K230 视觉网关</title><style>body{max-width:680px;margin:32px auto;background:#111;color:#eee;"
    "font-family:sans-serif;padding:16px}button{padding:10px 18px;background:#1677ff;color:#fff;"
    "border:0;border-radius:4px;font-size:16px}pre{background:#222;padding:12px;white-space:pre-wrap}</style></head><body>"
    "<h1>K230 视觉网关</h1><p>链路：<span id=\"link\">检测中</span></p>"
    "<p>本地视觉结果（K230）：</p><pre id=\"result\">等待结果…</pre>"
    "<button id=\"analyzeButton\" onclick=\"analyze()\">拍照并交给大模型分析</button>"
    "<p>大模型回答：</p><pre id=\"answer\">等待分析…</pre>"
    "<script>let analyzing=false;async function refresh(){if(analyzing)return;try{const d=await (await fetch('/vision')).json();"
    "link.textContent=d.online?'在线':'离线';result.textContent=d.result?JSON.stringify(d.result,null,2):'暂无';}catch(e){link.textContent='错误'}}"
    "async function analyze(){if(analyzing)return;analyzing=true;const button=document.getElementById('analyzeButton');button.disabled=true;answer.textContent='请求中…';const controller=new AbortController();const timer=setTimeout(()=>controller.abort(),75000);try{const r=await fetch('/analyze',{signal:controller.signal});const text=await r.text();if(!r.ok)throw new Error(text||('HTTP '+r.status));answer.textContent=text}catch(e){answer.textContent=e.name==='AbortError'?'请求超时，请检查串口日志':'请求失败：'+e.message}finally{clearTimeout(timer);analyzing=false;button.disabled=false}}"
    "setInterval(refresh,1000);refresh()</script></body></html>";

/**
 * 配网页 (SoftAP 模式)
 * 展示 Wi-Fi 凭据输入表单，提交后通过 POST /provision 保存。
 */
static const char s_provisioning_html[] =
    "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>OV2640 配网</title><style>body{max-width:420px;margin:32px auto;font-family:sans-serif;padding:16px}"
    "input,button{box-sizing:border-box;width:100%;padding:12px;margin:8px 0}button{background:#1677ff;color:#fff;border:0}"
    "</style></head><body><h1>连接 Wi-Fi</h1><form method=\"post\" action=\"/provision\">"
    "<input name=\"ssid\" maxlength=\"32\" placeholder=\"Wi-Fi 名称\" required>"
    "<input type=\"password\" name=\"password\" maxlength=\"63\" placeholder=\"Wi-Fi 密码\">"
    "<button type=\"submit\">保存并连接</button></form></body></html>";

/**
 * 配网提交成功页面
 */
static const char s_provisioning_success_html[] =
    "<!doctype html><html lang=\"zh-CN\"><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>OV2640 配网</title><body><h1>凭据已保存</h1><p>设备正在切换到站点模式，请连接目标 Wi-Fi 后访问设备 IP。</p></body></html>";

// POST /provision 请求体最大长度 (SSID + password + 分隔符)
#define WEB_PROVISIONING_BODY_MAX_LEN 384

/**
 * @brief 根路径处理函数 (GET /)
 *
 * 根据当前 Wi-Fi 工作模式返回不同的 HTML 页面：
 *   - 配网模式：返回 s_provisioning_html
 *   - 其他模式：返回 s_index_html (包含视觉状态和分析功能)
 *
 * @param request HTTP 请求对象
 * @return ESP_OK 成功，否则错误码
 */
static esp_err_t index_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    const char *page = wifi_is_provisioning() ? s_provisioning_html : s_index_html;
    return httpd_resp_send(request, page, HTTPD_RESP_USE_STRLEN);
}

/**
 * @brief 配网提交处理函数 (POST /provision)
 *
 * 从请求 body 中解析出 Wi-Fi 凭据 (SSID + password)，
 * 保存至 NVS，然后触发设备切换回 STA 模式连接该网络。
 *
 * @param request HTTP 请求对象
 * @return ESP_OK 保存并切换成功，否则返回错误码
 */
static esp_err_t provisioning_handler(httpd_req_t *request)
{
    // 安全检查：拒绝过短或过长的 body
    if (request->content_len <= 0 || request->content_len >= WEB_PROVISIONING_BODY_MAX_LEN) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "无效的 Wi-Fi 配置");
        return ESP_FAIL;
    }

    // 读取整个请求体
    char body[WEB_PROVISIONING_BODY_MAX_LEN];
    size_t received = 0;
    while (received < (size_t)request->content_len) {
        int read = httpd_req_recv(request, body + received, request->content_len - received);
        if (read <= 0) {
            httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "读取配置失败");
            return ESP_FAIL;
        }
        received += (size_t)read;
    }
    body[received] = '\0';  // 确保字符串终止

    // 解析表单数据 (application/x-www-form-urlencoded)
    wifi_credentials_t credentials;
    if (!web_parse_provisioning_form(body, &credentials)) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "SSID 或密码格式无效");
        return ESP_ERR_INVALID_ARG;
    }

    // 保存凭据并切换到 STA 模式
    esp_err_t err = wifi_save_credentials(credentials.ssid, credentials.password);
    if (err == ESP_OK) {
        err = wifi_start_station();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "应用配网凭据失败：%s", esp_err_to_name(err));
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "保存配置或切换模式失败");
        return err;
    }

    // 返回成功页面
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, s_provisioning_success_html, HTTPD_RESP_USE_STRLEN);
}

/**
 * @brief 视觉状态查询接口 (GET /vision)
 *
 * 返回一个 JSON 对象，包含：
 *   - online: K230 链路是否存活
 *   - age_ms: 最后一次视觉结果距今的毫秒数 (-1 表示从未收到)
 *   - result: 最新的视觉检测结果 JSON (null 表示无数据)
 *
 * @param request HTTP 请求对象
 * @return ESP_OK 成功，否则错误码
 */
static esp_err_t vision_handler(httpd_req_t *request)
{
    char result[VISION_STATE_MAX_JSON];
    int64_t age_ms = -1;
    // 从 vision_state 模块拷贝最新的结果及时间戳
    esp_err_t err = vision_state_copy_result(result, sizeof(result), &age_ms);
    if (err != ESP_OK) {
        return err;
    }

    char body[VISION_STATE_MAX_JSON + 128];
    const char *result_json = result[0] == '\0' ? "null" : result;
    // 组装 JSON 响应
    int written = snprintf(body, sizeof(body), "{\"online\":%s,\"age_ms\":%lld,\"result\":%s}",
                           k230_link_is_online() ? "true" : "false",
                           (long long)age_ms, result_json);
    if (written < 0 || written >= (int)sizeof(body)) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "视觉结果过长");
        return ESP_FAIL;
    }

    httpd_resp_set_type(request, "application/json; charset=utf-8");
    return httpd_resp_send(request, body, written);
}

/**
 * @brief 大模型分析接口 (GET /analyze)
 *
 * 触发 K230 拍摄一帧 JPEG 图像，并交由本地或云端大模型进行分析，
 * 分析结果以纯文本形式返回给前端。
 *
 * @param request HTTP 请求对象
 * @return ESP_OK 分析成功，否则错误码
 */
static esp_err_t analyze_handler(httpd_req_t *request)
{
    ESP_LOGI(TAG, "Analyze request started");
    ESP_LOGI(TAG, "Heap before analyze: free=%u internal=%u psram=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    char answer[1024];
    // 调用大模型分析接口，传入最新获取的 JPEG 图像
    esp_err_t err = llm_analyze_latest_image(answer, sizeof(answer));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Analyze request failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "调用视觉模型失败，请查看串口日志");
        return err;
    }

    ESP_LOGI(TAG, "Analyze request completed");
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_send(request, answer, HTTPD_RESP_USE_STRLEN);
}

/**
 * @brief 启动 HTTP 服务器并注册所有路由
 *
 * 该函数通常由 app_main() 调用一次，启动后各接口即可通过浏览器访问。
 * 配置：
 *   - 最大 URI 处理器数：5
 *   - 启用 LRU 缓存清空
 *   - 栈大小 8192 字节
 *
 * @return ESP_OK 成功，否则错误码
 */
esp_err_t web_start(void)
{
    if (s_server != NULL) {
        return ESP_ERR_INVALID_STATE;  // 防止重复启动
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 5;
    config.lru_purge_enable = true;
    config.stack_size = 8192;

    // 启动 HTTP 服务
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "HTTP 服务启动失败");

    // 注册各个 URI 路由
    const httpd_uri_t index_uri = {.uri = "/", .method = HTTP_GET, .handler = index_handler};
    const httpd_uri_t vision_uri = {.uri = "/vision", .method = HTTP_GET, .handler = vision_handler};
    const httpd_uri_t analyze_uri = {.uri = "/analyze", .method = HTTP_GET, .handler = analyze_handler};
    const httpd_uri_t provisioning_uri = {.uri = "/provision", .method = HTTP_POST, .handler = provisioning_handler};

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &index_uri), TAG, "主页路由注册失败");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &vision_uri), TAG, "视觉状态路由注册失败");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &analyze_uri), TAG, "模型路由注册失败");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &provisioning_uri), TAG, "配网页面路由注册失败");

    ESP_LOGI(TAG, "HTTP 服务已启动，等待浏览器连接");
    return ESP_OK;
}
