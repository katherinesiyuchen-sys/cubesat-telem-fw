#pragma once

#include "esp_err.h"
#include "packet.h"

esp_err_t telemetry_protocol_build(hope_packet_t *pkt);