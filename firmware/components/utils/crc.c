#include "crc.h"

uint16_t crc16_ccitt(const uint8_t *data, size_t len, uint16_t seed) {
    uint16_t crc = seed;
    if (data == NULL) {
        return crc;
    }

    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint32_t crc32_ieee(const uint8_t *data, size_t len, uint32_t seed) {
    uint32_t crc = ~seed;
    if (data == NULL) {
        return ~crc;
    }

    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) ? (crc >> 1) ^ 0xEDB88320UL : (crc >> 1);
        }
    }
    return ~crc;
}
