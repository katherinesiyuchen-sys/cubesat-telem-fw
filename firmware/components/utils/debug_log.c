#include "debug_log.h"

void debug_log_boot_banner(const char *component) {
    const char *name = component == NULL ? "unknown" : component;
    ESP_LOGI(name, "CubeSat component online");
}
