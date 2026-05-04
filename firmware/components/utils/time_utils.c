#include "time_utils.h"

#include <stdbool.h>

#include "esp_timer.h"

uint64_t time_utils_now_us(void) {
    return (uint64_t)esp_timer_get_time();
}

uint32_t time_utils_now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

uint32_t time_utils_now_s(void) {
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

bool time_utils_elapsed_ms(uint32_t start_ms, uint32_t interval_ms) {
    return (uint32_t)(time_utils_now_ms() - start_ms) >= interval_ms;
}
