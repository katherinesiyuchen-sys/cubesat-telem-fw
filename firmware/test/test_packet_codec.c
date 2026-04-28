#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "packet.h"
#include "packet_codec.h"
#include "telemetry_protocol.h"
#include "session.h"
#include "replay.h"

static int test_packet_encode_decode(void) {
    hope_packet_t original;
    hope_packet_t decoded;
    uint8_t buffer[HOPE_MAX_PACKET_LEN];

    int build_result = telemetry_protocol_build_fake(&original);
    if (build_result != 0) {
        printf("FAIL: telemetry_protocol_build_fake returned %d\n", build_result);
        return 1;
    }

    session_init(0x12345678);

    original.session_id = session_get_id();
    original.counter = session_next_counter();

    int encoded_len = packet_encode(&original, buffer, sizeof(buffer));
    if (encoded_len <= 0) {
        printf("FAIL: packet_encode returned %d\n", encoded_len);
        return 1;
    }

    int decode_result = packet_decode(buffer, (size_t)encoded_len, &decoded);
    if (decode_result != 0) {
        printf("FAIL: packet_decode returned %d\n", decode_result);
        return 1;
    }

    if (decoded.version != original.version) {
        printf("FAIL: version mismatch\n");
        return 1;
    }

    if (decoded.type != original.type) {
        printf("FAIL: type mismatch\n");
        return 1;
    }

    if (decoded.src_id != original.src_id) {
        printf("FAIL: src_id mismatch\n");
        return 1;
    }

    if (decoded.dst_id != original.dst_id) {
        printf("FAIL: dst_id mismatch\n");
        return 1;
    }

    if (decoded.session_id != original.session_id) {
        printf("FAIL: session_id mismatch\n");
        return 1;
    }

    if (decoded.counter != original.counter) {
        printf("FAIL: counter mismatch\n");
        return 1;
    }

    if (decoded.timestamp != original.timestamp) {
        printf("FAIL: timestamp mismatch\n");
        return 1;
    }

    if (decoded.payload_len != original.payload_len) {
        printf("FAIL: payload_len mismatch\n");
        return 1;
    }

    if (memcmp(decoded.payload, original.payload, original.payload_len) != 0) {
        printf("FAIL: payload mismatch\n");
        return 1;
    }

    printf("PASS: packet encode/decode\n");
    return 0;
}

static int test_session_counter(void) {
    session_init(0xAABBCCDD);

    if (session_get_id() != 0xAABBCCDD) {
        printf("FAIL: session ID mismatch\n");
        return 1;
    }

    uint32_t c1 = session_next_counter();
    uint32_t c2 = session_next_counter();
    uint32_t c3 = session_next_counter();

    if (c1 != 1 || c2 != 2 || c3 != 3) {
        printf("FAIL: session counter sequence incorrect: %lu %lu %lu\n",
            (unsigned long)c1,
            (unsigned long)c2,
            (unsigned long)c3);
        return 1;
    }

    printf("PASS: session counter\n");
    return 0;
}

static int test_replay_protection(void) {
    replay_init();

    if (replay_check_and_update(0)) {
        printf("FAIL: replay accepted counter 0\n");
        return 1;
    }

    if (!replay_check_and_update(1)) {
        printf("FAIL: replay rejected fresh counter 1\n");
        return 1;
    }

    if (replay_check_and_update(1)) {
        printf("FAIL: replay accepted duplicate counter 1\n");
        return 1;
    }

    if (!replay_check_and_update(2)) {
        printf("FAIL: replay rejected fresh counter 2\n");
        return 1;
    }

    if (replay_check_and_update(1)) {
        printf("FAIL: replay accepted old counter 1 after counter 2\n");
        return 1;
    }

    printf("PASS: replay protection\n");
    return 0;
}

int main(void) {
    int failures = 0;

    failures += test_packet_encode_decode();
    failures += test_session_counter();
    failures += test_replay_protection();

    if (failures == 0) {
        printf("ALL TESTS PASS\n");
        return 0;
    }

    printf("%d TEST GROUP(S) FAILED\n", failures);
    return 1;
}