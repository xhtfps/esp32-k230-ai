/**
 * @file vision_state.h
 * @brief 视觉任务状态管理模块
 *
 * 缓存 K230 上报的人脸/目标检测 JSON 结果与 JPEG 图像，
 * 处理 JPEG 的分块接收、校验、存储与获取，并提供线程安全的访问接口。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "k230_protocol.h"

/** 最大 JSON 结果字符串长度（含结束符） */
#define VISION_STATE_MAX_JSON       1024U
/** JPEG 缓冲区最大容量（256 KB） */
#define VISION_STATE_MAX_JPEG       (256U * 1024U)
/** JPEG 数据块帧头长度：请求 ID（2 字节） + 偏移（4 字节） */
#define VISION_STATE_JPEG_CHUNK_HDR 6U
/** JPEG 完成帧负载长度：请求 ID（2 字节） + 总长度（4 字节） + CRC32（4 字节） */
#define VISION_STATE_JPEG_DONE_SIZE 10U

/**
 * @brief 初始化视觉状态模块
 *
 * 分配内部缓冲区并初始化状态变量。应在系统启动时调用一次。
 * @return ESP_OK 成功，否则错误码。
 */
esp_err_t vision_state_init(void);

/**
 * @brief 准备一次 JPEG 传输
 *
 * 在向 K230 发送 JPEG 请求前调用，设置内部状态以接收后续数据块。
 * @param request_id JPEG 请求的标识 ID。
 * @return ESP_OK 成功，否则错误码。
 */
esp_err_t vision_state_prepare_jpeg(uint16_t request_id);

/**
 * @brief 存储 K230 上报的 JSON 推理结果
 *
 * 线程安全地复制 JSON 文本到内部缓存。
 * @param json   指向 JSON 数据的指针（不以 '\0' 结尾亦可）。
 * @param length JSON 数据长度。
 * @return ESP_OK 成功，否则错误码。
 */
esp_err_t vision_state_accept_result(const uint8_t *json, size_t length);

/**
 * @brief 开始接收 JPEG 数据
 *
 * 对应 K230_FRAME_JPEG_CHUNK 帧到达时调用，用于初始化本次 JPEG 传输会话。
 * @param request_id 帧中的请求 ID。
 * @return ESP_OK 成功，否则错误码。
 */
esp_err_t vision_state_begin_jpeg(uint16_t request_id);

/**
 * @brief 追加一个 JPEG 数据块
 *
 * 将接收到的 JPEG 数据块写入内部缓冲区。
 * @param chunk  数据块指针。
 * @param length 数据块长度。
 * @return ESP_OK 成功，否则错误码（如缓冲区溢出）。
 */
esp_err_t vision_state_append_jpeg(const uint8_t *chunk, size_t length);

/**
 * @brief 完成 JPEG 传输并进行完整性校验
 *
 * 当收到 JPEG 完成帧（K230_FRAME_JPEG_COMPLETE）时调用。
 * 校验接收到的总长度与 CRC32，校验通过后可通过 vision_state_acquire_jpeg 获取。
 *
 * @param request_id      请求 ID。
 * @param expected_length 预期的 JPEG 总长度。
 * @param expected_crc32  预期的 CRC32 校验值。
 * @param[out] jpeg       接收指向完整 JPEG 数据的指针（只读）。
 * @param[out] jpeg_length 输出 JPEG 数据长度。
 * @return ESP_OK 校验通过并可用，否则错误码。
 */
esp_err_t vision_state_finish_jpeg(uint16_t request_id, uint32_t expected_length,
                                   uint32_t expected_crc32, const uint8_t **jpeg,
                                   size_t *jpeg_length);

/**
 * @brief 处理 JPEG 传输错误
 *
 * 当 K230 报告 JPEG 错误时调用，标记当前请求 ID 为失败状态。
 * @param request_id 出错的请求 ID。
 * @return ESP_OK 成功，否则错误码。
 */
esp_err_t vision_state_accept_jpeg_error(uint16_t request_id);

/**
 * @brief 查询指定请求 ID 是否存在 JPEG 错误
 *
 * @param request_id 请求 ID。
 * @return true  该请求曾报告错误。
 * @return false 无错误或 ID 不存在。
 */
bool vision_state_has_jpeg_error(uint16_t request_id);

/**
 * @brief 获取已完成 JPEG 图像的只读指针
 *
 * 通过 vision_state_finish_jpeg 校验成功的 JPEG 可通过此函数获取。
 * 调用者使用完毕后必须调用 vision_state_release_jpeg 释放引用。
 *
 * @param[out] jpeg       指向 JPEG 数据的指针。
 * @param[out] jpeg_length JPEG 数据长度。
 * @return ESP_OK 成功，否则错误码（如无可用 JPEG）。
 */
esp_err_t vision_state_acquire_jpeg(const uint8_t **jpeg, size_t *jpeg_length);

/**
 * @brief 释放 JPEG 引用计数
 *
 * 与 vision_state_acquire_jpeg 配对使用。
 * @param jpeg 来自 vision_state_acquire_jpeg 的指针。
 */
void vision_state_release_jpeg(const uint8_t *jpeg);

/**
 * @brief 中止当前进行中的 JPEG 传输
 *
 * 释放为当前不完整 JPEG 分配的资源。
 */
void vision_state_abort_jpeg(void);

/**
 * @brief 处理来自 K230 的协议帧
 *
 * 根据帧类型调用相应的处理函数（结果/JPEG/心跳等），
 * 是链路层与业务逻辑之间的分发入口。
 *
 * @param frame 解码后的协议帧指针。
 * @return ESP_OK 成功，否则错误码。
 */
esp_err_t vision_state_process_frame(const k230_protocol_frame_t *frame);

/**
 * @brief 设置 K230 链路在线状态
 *
 * 由链路管理层调用，用于更新 Web 接口返回的在线状态。
 * @param online true 表示在线，false 表示离线。
 */
void vision_state_set_link_online(bool online);

/**
 * @brief 查询 K230 链路是否在线
 *
 * @return true  在线。
 * @return false 离线。
 */
bool vision_state_is_link_online(void);

/**
 * @brief 获取最新的 JSON 结果及其年龄
 *
 * 将内部缓存的 JSON 结果复制到 output 缓冲区。
 * 如果从未收到结果，则 output 为空字符串（'\0'）。
 *
 * @param[out] output     输出缓冲区。
 * @param      output_size 缓冲区大小。
 * @param[out] age_ms     结果距今的毫秒数，若从未收到则为 -1。
 * @return ESP_OK 成功，否则错误码（如缓冲区过小）。
 */
esp_err_t vision_state_copy_result(char *output, size_t output_size, int64_t *age_ms);
