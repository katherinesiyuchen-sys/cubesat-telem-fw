#pragma once

#include <stdbool.h>
#include <stdint.h>

uint64_t time_utils_now_us(void);
uint32_t time_utils_now_ms(void);
uint32_t time_utils_now_s(void);
bool time_utils_elapsed_ms(uint32_t start_ms, uint32_t interval_ms);
