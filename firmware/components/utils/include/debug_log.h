#pragma once

#include "esp_log.h"

#define CUBESAT_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#define CUBESAT_LOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#define CUBESAT_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)

void debug_log_boot_banner(const char *component);
