/**
 * @file vision_state.c
 * @brief 视觉任务状态管理实现
 *
 * 负责缓存 K230 上报的 JSON 推理结果和 JPEG 图像，
 * 管理 JPEG 分块接收、重组与 CRC32 校验。
 * 所有操作通过互斥锁保证线程安全。
 */

#include "vision_state.h"

#include <string.h>

#include "esp_heap_caps.h"   // 堆内存分配（支持 SPIRAM）
#include "esp_log.h"         // 日志系统
#include "esp_timer.h"       // 高精度定时器
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h" // 信号量

static const char *TAG = "VISION_STATE"; // 日志标签

// ---------- 全局状态（受 s_lock 保护） ----------

static SemaphoreHandle_t s_lock;          /**< 互斥锁，保护所有状态字段 */
static char s_latest_json[VISION_STATE_MAX_JSON]; /**< 最新 JSON 推理结果 */
static int64_t s_latest_result_us;        /**< 最新结果到达时刻（微秒，系统启动计时） */
static bool s_link_online;                /**< K230 链路在线标志 */

static uint8_t *s_jpeg;                   /**< 当前 JPEG 缓冲区指针 */
static size_t s_jpeg_length;              /**< 已接收的 JPEG 数据长度 */
static uint16_t s_jpeg_request_id;        /**< 当前 JPEG 传输的请求 ID */
static bool s_jpeg_active;                /**< 是否正在进行 JPEG 分块接收 */
static bool s_jpeg_completed;             /**< JPEG 传输是否已完成并通过校验 */
static bool s_jpeg_error;                 /**< 是否有 JPEG 错误标记 */
static uint16_t s_jpeg_error_request_id;  /**< 发生错误的请求 ID */

// ---------- 内部辅助函数 ----------

/**
 * @brief 计算 CRC32 校验值
 *
 * 使用标准 CRC-32/MPEG-2 多项式 0xEDB88320。
 *
 * @param data   待校验数据
 * @param length 数据长度
 * @return 32 位 CRC 值
 */
static uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0U ? (crc >> 1) ^ 0xEDB88320U : crc >> 1;
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

/**
 * @brief 尝试获取互斥锁（1 秒超时）
 *
 * @return ESP_OK 如果成功获取锁，否则 ESP_ERR_TIMEOUT
 */
static esp_err_t lock_state(void)
{
    return s_lock != NULL && xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE
               ? ESP_OK : ESP_ERR_TIMEOUT;
}

// ---------- 公共接口实现 ----------

esp_err_t vision_state_init(void)
{
    if (s_lock != NULL) {
        return ESP_ERR_INVALID_STATE; // 已初始化，防止重复
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // 重置所有状态至初始值
    memset(s_latest_json, 0, sizeof(s_latest_json));
    s_latest_result_us = 0;
    s_link_online = false;
    s_jpeg = NULL;
    s_jpeg_length = 0;
    s_jpeg_active = false;
    s_jpeg_completed = false;
    s_jpeg_error = false;
    s_jpeg_error_request_id = 0;
    return ESP_OK;
}

esp_err_t vision_state_accept_result(const uint8_t *json, size_t length)
{
    if (json == NULL || length == 0 || length >= VISION_STATE_MAX_JSON) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (lock_state() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }

    // 复制 JSON 并追加结束符
    memcpy(s_latest_json, json, length);
    s_latest_json[length] = '\0';
    s_latest_result_us = esp_timer_get_time(); // 记录接收时间
    s_link_online = true;                      // 收到有效数据即认为在线

    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t vision_state_prepare_jpeg(uint16_t request_id)
{
    if (lock_state() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    // 如果正在进行 JPEG 传输或已完成，无法准备新的请求
    if (s_jpeg_active || s_jpeg_completed) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_jpeg_request_id = request_id;
    s_jpeg_error = false;
    s_jpeg_error_request_id = 0;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t vision_state_begin_jpeg(uint16_t request_id)
{
    if (lock_state() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_jpeg_active || s_jpeg_completed) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    // 优先从 SPIRAM 分配（如果可用），否则使用内部 RAM
    s_jpeg = heap_caps_malloc(VISION_STATE_MAX_JPEG, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_jpeg == NULL) {
        s_jpeg = heap_caps_malloc(VISION_STATE_MAX_JPEG, MALLOC_CAP_8BIT);
    }
    if (s_jpeg == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NO_MEM;
    }

    s_jpeg_length = 0;
    s_jpeg_request_id = request_id;
    s_jpeg_active = true;
    s_jpeg_completed = false;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t vision_state_append_jpeg(const uint8_t *chunk, size_t length)
{
    if (chunk == NULL || length == 0 || length > 2048U) {
        return ESP_ERR_INVALID_SIZE; // 单个块不应超过协议上限
    }
    if (lock_state() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    // 仅在活动状态下且缓冲区足够时才追加
    if (!s_jpeg_active || s_jpeg == NULL || s_jpeg_length + length > VISION_STATE_MAX_JPEG) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(s_jpeg + s_jpeg_length, chunk, length);
    s_jpeg_length += length;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t vision_state_finish_jpeg(uint16_t request_id, uint32_t expected_length,
                                   uint32_t expected_crc32, const uint8_t **jpeg,
                                   size_t *jpeg_length)
{
    if (jpeg == NULL || jpeg_length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (lock_state() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }

    // 校验请求 ID、数据长度和 CRC32
    if (!s_jpeg_active || s_jpeg == NULL || request_id != s_jpeg_request_id ||
        expected_length != s_jpeg_length || crc32(s_jpeg, s_jpeg_length) != expected_crc32) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_CRC;
    }

    // 传输成功，标记为已完成，并将缓冲区指针交给调用者
    s_jpeg_active = false;
    s_jpeg_completed = true;
    *jpeg = s_jpeg;
    *jpeg_length = s_jpeg_length;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t vision_state_accept_jpeg_error(uint16_t request_id)
{
    if (lock_state() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    // 只接受当前活动请求的错误，且不能覆盖已完成的结果
    if (s_jpeg_completed || request_id != s_jpeg_request_id) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    // 释放已分配但未完成的缓冲区
    if (s_jpeg != NULL) {
        heap_caps_free(s_jpeg);
    }
    s_jpeg = NULL;
    s_jpeg_length = 0;
    s_jpeg_active = false;
    s_jpeg_completed = false;
    s_jpeg_error = true;
    s_jpeg_error_request_id = request_id;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

bool vision_state_has_jpeg_error(uint16_t request_id)
{
    bool has_error = false;
    if (lock_state() == ESP_OK) {
        has_error = s_jpeg_error && s_jpeg_error_request_id == request_id;
        xSemaphoreGive(s_lock);
    }
    return has_error;
}

esp_err_t vision_state_acquire_jpeg(const uint8_t **jpeg, size_t *jpeg_length)
{
    if (jpeg == NULL || jpeg_length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (lock_state() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_jpeg_completed || s_jpeg == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND; // 无可用 JPEG
    }
    *jpeg = s_jpeg;
    *jpeg_length = s_jpeg_length;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

void vision_state_release_jpeg(const uint8_t *jpeg)
{
    if (lock_state() != ESP_OK) {
        return;
    }
    // 仅当传入指针与当前缓冲区一致时才释放
    if (jpeg == s_jpeg) {
        heap_caps_free(s_jpeg);
        s_jpeg = NULL;
        s_jpeg_length = 0;
        s_jpeg_completed = false;
    }
    xSemaphoreGive(s_lock);
}

void vision_state_abort_jpeg(void)
{
    if (lock_state() != ESP_OK) {
        return;
    }
    // 无条件释放 JPEG 缓冲区并重置所有 JPEG 状态
    if (s_jpeg != NULL) {
        heap_caps_free(s_jpeg);
    }
    s_jpeg = NULL;
    s_jpeg_length = 0;
    s_jpeg_active = false;
    s_jpeg_completed = false;
    s_jpeg_error = false;
    s_jpeg_error_request_id = 0;
    xSemaphoreGive(s_lock);
}

esp_err_t vision_state_process_frame(const k230_protocol_frame_t *frame)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 根据帧类型分发到对应的处理函数
    switch (frame->type) {
    case K230_FRAME_RESULT:
        return vision_state_accept_result(frame->payload, frame->payload_length);
    case K230_FRAME_HEARTBEAT:
        vision_state_set_link_online(true);
        return ESP_OK;
    case K230_FRAME_JPEG_CHUNK: {
        if (frame->payload_length <= VISION_STATE_JPEG_CHUNK_HDR) {
            return ESP_ERR_INVALID_SIZE;
        }
        // 解析头：请求 ID（2 字节） + 偏移（4 字节），小端序
        uint16_t request_id = (uint16_t)frame->payload[0] |
                              ((uint16_t)frame->payload[1] << 8);
        uint32_t offset = (uint32_t)frame->payload[2] |
                          ((uint32_t)frame->payload[3] << 8) |
                          ((uint32_t)frame->payload[4] << 16) |
                          ((uint32_t)frame->payload[5] << 24);
        // 第一个块（offset == 0）需初始化缓冲区
        if (offset == 0U && vision_state_begin_jpeg(request_id) != ESP_OK) {
            return ESP_ERR_INVALID_STATE;
        }
        // 校验偏移量是否连续
        if (lock_state() != ESP_OK) {
            return ESP_ERR_TIMEOUT;
        }
        bool offset_matches = s_jpeg_active && request_id == s_jpeg_request_id &&
                              offset == s_jpeg_length;
        xSemaphoreGive(s_lock);
        if (!offset_matches) {
            vision_state_abort_jpeg(); // 序列错误，中止传输
            return ESP_ERR_INVALID_STATE;
        }
        // 将数据部分追加到缓冲区
        return vision_state_append_jpeg(frame->payload + VISION_STATE_JPEG_CHUNK_HDR,
                                        frame->payload_length - VISION_STATE_JPEG_CHUNK_HDR);
    }
    case K230_FRAME_JPEG_COMPLETE: {
        if (frame->payload_length != VISION_STATE_JPEG_DONE_SIZE) {
            return ESP_ERR_INVALID_SIZE;
        }
        // 解析完成帧：请求 ID(2) + 总长度(4) + CRC32(4)，小端序
        uint16_t request_id = (uint16_t)frame->payload[0] |
                              ((uint16_t)frame->payload[1] << 8);
        uint32_t length = (uint32_t)frame->payload[2] |
                          ((uint32_t)frame->payload[3] << 8) |
                          ((uint32_t)frame->payload[4] << 16) |
                          ((uint32_t)frame->payload[5] << 24);
        uint32_t checksum = (uint32_t)frame->payload[6] |
                            ((uint32_t)frame->payload[7] << 8) |
                            ((uint32_t)frame->payload[8] << 16) |
                            ((uint32_t)frame->payload[9] << 24);
        const uint8_t *jpeg = NULL;
        size_t jpeg_length = 0;
        return vision_state_finish_jpeg(request_id, length, checksum, &jpeg, &jpeg_length);
    }
    case K230_FRAME_ERROR: {
        if (frame->payload_length < 2U) {
            return ESP_ERR_INVALID_SIZE;
        }
        uint16_t request_id = (uint16_t)frame->payload[0] |
                              ((uint16_t)frame->payload[1] << 8);
        return vision_state_accept_jpeg_error(request_id);
    }
    default:
        ESP_LOGD(TAG, "忽略未知帧类型：0x%02x", frame->type);
        return ESP_ERR_NOT_SUPPORTED;
    }
}

void vision_state_set_link_online(bool online)
{
    if (lock_state() == ESP_OK) {
        s_link_online = online;
        xSemaphoreGive(s_lock);
    }
}

bool vision_state_is_link_online(void)
{
    bool online = false;
    if (lock_state() == ESP_OK) {
        online = s_link_online;
        xSemaphoreGive(s_lock);
    }
    return online;
}

esp_err_t vision_state_copy_result(char *output, size_t output_size, int64_t *age_ms)
{
    if (output == NULL || output_size == 0 || age_ms == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (lock_state() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_latest_json[0] == '\0') {
        // 尚未收到任何结果
        output[0] = '\0';
        *age_ms = -1;
    } else {
        strlcpy(output, s_latest_json, output_size);
        *age_ms = (esp_timer_get_time() - s_latest_result_us) / 1000; // 微秒转毫秒
    }
    xSemaphoreGive(s_lock);
    return ESP_OK;
}
