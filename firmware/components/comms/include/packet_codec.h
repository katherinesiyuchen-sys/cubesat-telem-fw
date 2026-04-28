#pragma once

#include <stddef.h>
#include <stdint.h>
#include "packet.h"

int packet_encode(const hope_packet_t *pkt, uint8_t *out_buf, size_t out_len);
int packet_decode(const uint8_t *buf, size_t len, hope_packet_t *out_pkt);

// Public API for encoding/decoding packets, which may include encryption/authentication in the future.