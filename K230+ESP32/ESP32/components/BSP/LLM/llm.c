#include "llm.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "k230_link.h"
#include "llm_config.h"
#include "vision_state.h"

static const char *TAG = "LLM";

#define LLM_HTTP_TIMEOUT_MS 60000
#define LLM_RESPONSE_MAX 4096

typedef struct {
    char *buffer;
    size_t length;
    size_t capacity;
    bool truncated;
} llm_response_capture_t;

static esp_err_t llm_http_event_handler(esp_http_client_event_t *event)
{
    if (event == NULL || event->user_data == NULL ||
        event->event_id != HTTP_EVENT_ON_DATA || event->data == NULL || event->data_len <= 0) {
        return ESP_OK;
    }

    llm_response_capture_t *capture = event->user_data;
    size_t available = capture->capacity > capture->length + 1
                           ? capture->capacity - capture->length - 1
                           : 0;
    size_t copy_length = (size_t)event->data_len < available
                             ? (size_t)event->data_len : available;
    if (copy_length > 0) {
        memcpy(capture->buffer + capture->length, event->data, copy_length);
        capture->length += copy_length;
        capture->buffer[capture->length] = '\0';
    }
    if (copy_length != (size_t)event->data_len) {
        capture->truncated = true;
    }
    return ESP_OK;
}

static char *find_json_string_value(char *json, const char *key)
{
    char needle[32];
    int written = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (written < 0 || written >= (int)sizeof(needle)) {
        return NULL;
    }

    char *match = strstr(json, needle);
    while (match != NULL) {
        char *cursor = match + written;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') {
            cursor++;
        }
        if (*cursor == ':') {
            cursor++;
            while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') {
                cursor++;
            }
            if (*cursor == '"') {
                return cursor + 1;
            }
        }
        match = strstr(match + written, needle);
    }
    return NULL;
}

static void copy_json_string(char *destination, size_t destination_size, char *source)
{
    if (destination_size == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }

    size_t source_index = 0;
    size_t destination_index = 0;
    while (source[source_index] != '\0' && source[source_index] != '"' &&
           destination_index + 1 < destination_size) {
        if (source[source_index] == '\\' && source[source_index + 1] != '\0') {
            source_index++;
            switch (source[source_index]) {
            case 'n': destination[destination_index++] = '\n'; break;
            case 'r': destination[destination_index++] = '\r'; break;
            case 't': destination[destination_index++] = '\t'; break;
            default: destination[destination_index++] = source[source_index]; break;
            }
            source_index++;
            continue;
        }
        destination[destination_index++] = source[source_index++];
    }
    destination[destination_index] = '\0';
}

static esp_err_t llm_post_jpeg(const uint8_t *jpeg, size_t jpeg_length,
                               char *answer, size_t answer_size)
{
    if (jpeg == NULL || jpeg_length == 0 || answer == NULL || answer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    answer[0] = '\0';

    const char *prefix = "{\"model\":\"" LLM_MODEL
        "\",\"messages\":[{\"role\":\"user\",\"content\":["
        "{\"type\":\"text\",\"text\":\"请简洁描述图片中的主要内容，并用中文回答。\"},"
        "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,";
    const char *suffix = "\"}}]}],\"stream\":false}";
    size_t base64_size = 4 * ((jpeg_length + 2) / 3) + 1;
    size_t body_size = strlen(prefix) + base64_size + strlen(suffix) + 1;
    char *body = heap_caps_malloc(body_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body == NULL) {
        body = malloc(body_size);
    }
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t encoded_size = 0;
    size_t prefix_len = strlen(prefix);
    int base64_err = mbedtls_base64_encode((unsigned char *)body + prefix_len, base64_size,
                                           &encoded_size, jpeg, jpeg_length);
    if (base64_err != 0) {
        free(body);
        return ESP_FAIL;
    }
    memcpy(body, prefix, prefix_len);
    memcpy(body + prefix_len + encoded_size, suffix, strlen(suffix) + 1);
    size_t final_body_len = prefix_len + encoded_size + strlen(suffix);
    bool api_key_configured = strncmp(LLM_API_KEY, "REPLACE_WITH_", 13) != 0;
    ESP_LOGI(TAG, "DashScope request body length=%u, API key configured=%s",
             (unsigned)final_body_len, api_key_configured ? "yes" : "no");

    char *response = heap_caps_malloc(LLM_RESPONSE_MAX,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (response == NULL) {
        response = malloc(LLM_RESPONSE_MAX);
    }
    if (response == NULL) {
        free(body);
        return ESP_ERR_NO_MEM;
    }
    response[0] = '\0';
    llm_response_capture_t capture = {
        .buffer = response,
        .length = 0,
        .capacity = LLM_RESPONSE_MAX,
        .truncated = false,
    };

    esp_http_client_config_t config = {
        .url = LLM_API_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = LLM_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .event_handler = llm_http_event_handler,
        .user_data = &capture,
    };
    ESP_LOGI(TAG, "Vision model endpoint: %s", LLM_API_URL);
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(response);
        free(body);
        return ESP_ERR_NO_MEM;
    }

    char authorization[sizeof("Bearer ") + sizeof(LLM_API_KEY)];
    snprintf(authorization, sizeof(authorization), "Bearer %s", LLM_API_KEY);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", authorization);
    esp_http_client_set_post_field(client, body, (int)final_body_len);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "模型响应：HTTP %d，声明长度 %d，实际读取 %u",
                 status, content_length, (unsigned)capture.length);
        if (capture.truncated) {
            ESP_LOGW(TAG, "视觉模型响应超过 %u 字节，已截断", (unsigned)(LLM_RESPONSE_MAX - 1));
        }
        if (capture.length == 0) {
            ESP_LOGE(TAG, "视觉模型返回空响应");
            err = ESP_FAIL;
        } else if (status < 200 || status >= 300) {
            int log_length = capture.length > 1024 ? 1024 : (int)capture.length;
            ESP_LOGE(TAG, "视觉模型错误响应：%.*s", log_length, response);
            err = ESP_FAIL;
        } else {
            char *answer_value = find_json_string_value(response, "content");
            copy_json_string(answer, answer_size, answer_value);
            if (answer[0] == '\0') {
                strlcpy(answer, response, answer_size);
            }
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "调用视觉模型失败：%s", esp_err_to_name(err));
    }
    free(response);
    esp_http_client_cleanup(client);
    free(body);
    return err;
}

static esp_err_t llm_request_k230_jpeg(const uint8_t **jpeg, size_t *jpeg_length)
{
    static uint16_t request_id;
    if (!k230_link_is_online()) {
        return ESP_ERR_INVALID_STATE;
    }

    request_id++;
    if (request_id == 0) {
        request_id = 1;
    }
    vision_state_abort_jpeg();
    esp_err_t err = vision_state_prepare_jpeg(request_id);
    if (err != ESP_OK) {
        return err;
    }
    err = k230_link_request_jpeg(request_id);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "K230 JPEG request sent: id=%u", (unsigned)request_id);

    int64_t deadline = esp_timer_get_time() + 10000000LL;
    while (esp_timer_get_time() < deadline) {
        err = vision_state_acquire_jpeg(jpeg, jpeg_length);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "K230 JPEG received: id=%u length=%u",
                     (unsigned)request_id, (unsigned)*jpeg_length);
            return ESP_OK;
        }
        if (vision_state_has_jpeg_error(request_id)) {
            ESP_LOGW(TAG, "K230 rejected JPEG request: id=%u", (unsigned)request_id);
            vision_state_abort_jpeg();
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vision_state_abort_jpeg();
    ESP_LOGW(TAG, "K230 JPEG timeout: id=%u", (unsigned)request_id);
    return ESP_ERR_TIMEOUT;
}

esp_err_t llm_analyze_latest_image(char *answer, size_t answer_size)
{
    const uint8_t *jpeg = NULL;
    size_t jpeg_length = 0;
    esp_err_t err = llm_request_k230_jpeg(&jpeg, &jpeg_length);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "K230 JPEG 请求失败：%s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Posting JPEG to vision model: length=%u", (unsigned)jpeg_length);
    err = llm_post_jpeg(jpeg, jpeg_length, answer, answer_size);
    vision_state_release_jpeg(jpeg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "模型回答：%s", answer[0] == '\0' ? "<空>" : answer);
    }
    return err;
}
