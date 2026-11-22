#pragma once

#include <stddef.h>

#include "esp_err.h"

/* Requests one K230 JPEG and asks the configured vision model to describe it. */
esp_err_t llm_analyze_latest_image(char *answer, size_t answer_size);
