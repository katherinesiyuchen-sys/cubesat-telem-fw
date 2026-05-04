#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "packet.h"
#include "packet_codec.h"
#include "command_protocol.h"
#include "telemetry_protocol.h"
#include "session.h"
#include "replay.h"
#include "gnss.h"

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

static int test_malformed_packet_rejection(void) {
    uint8_t short_header[HOPE_PACKET_HEADER_LEN - 1] = {0};
    hope_packet_t decoded;

    if (packet_decode(short_header, sizeof(short_header), &decoded) != -2) {
        printf("FAIL: malformed packet short header accepted\n");
        return 1;
    }

    uint8_t truncated_payload[HOPE_PACKET_HEADER_LEN + 1] = {0};
    truncated_payload[18] = 0;
    truncated_payload[19] = 2;

    if (packet_decode(truncated_payload, sizeof(truncated_payload), &decoded) != -4) {
        printf("FAIL: malformed packet truncated payload accepted\n");
        return 1;
    }

    printf("PASS: malformed packet rejection\n");
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

    if (!replay_check_session_and_update(0xAABBCCDD, 1)) {
        printf("FAIL: replay rejected fresh counter 1 on different session\n");
        return 1;
    }

    if (replay_check_session_and_update(0xAABBCCDD, 1)) {
        printf("FAIL: replay accepted duplicate counter on different session\n");
        return 1;
    }

    printf("PASS: replay protection\n");
    return 0;
}

static int test_gnss_parser(void) {
    gnss_fix_t fix;

    const char *valid_gga = "$GNGGA,123519,3748.2900,N,12216.3800,W,1,08,0.9,10.0,M,0.0,M,,*47";
    if (gnss_parse_sentence(valid_gga, &fix) != ESP_OK || !fix.valid) {
        printf("FAIL: GNSS parser rejected valid GGA\n");
        return 1;
    }

    if (fix.latitude_e7 != 378048333 || fix.longitude_e7 != -1222730000 || fix.satellites != 8) {
        printf(
            "FAIL: GNSS parser produced unexpected fix: lat=%ld lon=%ld sats=%u\n",
            (long)fix.latitude_e7,
            (long)fix.longitude_e7,
            (unsigned)fix.satellites
        );
        return 1;
    }

    const char *no_fix_gga = "$GNGGA,123519,3748.2900,N,12216.3800,W,0,00,0.9,10.0,M,0.0,M,,*47";
    if (gnss_parse_sentence(no_fix_gga, &fix) == ESP_OK) {
        printf("FAIL: GNSS parser accepted no-fix GGA\n");
        return 1;
    }

    const char *valid_rmc = "$GNRMC,123519,A,3748.2900,N,12216.3800,W,0.0,0.0,010126,,,A*68";
    if (gnss_parse_sentence(valid_rmc, &fix) != ESP_OK || !fix.valid) {
        printf("FAIL: GNSS parser rejected valid RMC\n");
        return 1;
    }

    const char *no_fix_rmc = "$GNRMC,123519,V,3748.2900,N,12216.3800,W,0.0,0.0,010126,,,N*68";
    if (gnss_parse_sentence(no_fix_rmc, &fix) == ESP_OK) {
        printf("FAIL: GNSS parser accepted no-fix RMC\n");
        return 1;
    }

    printf("PASS: GNSS parser\n");
    return 0;
}

static int test_command_protocol(void) {
    command_request_t request = {
        .version = COMMAND_PAYLOAD_VERSION,
        .command_id = 44,
        .opcode = COMMAND_OPCODE_PING,
        .flags = 0,
        .arg_len = 0,
    };
    hope_packet_t command_packet;
    uint8_t encoded[HOPE_MAX_PACKET_LEN];
    hope_packet_t decoded_packet;
    command_request_t decoded_request;

    if (command_protocol_build_request_packet(&request, 2, 1, 0x12345678, 44, 99, &command_packet) != ESP_OK) {
        printf("FAIL: command packet build failed\n");
        return 1;
    }
    int encoded_len = packet_encode(&command_packet, encoded, sizeof(encoded));
    if (encoded_len <= 0 || packet_decode(encoded, (size_t)encoded_len, &decoded_packet) != 0) {
        printf("FAIL: command packet encode/decode failed\n");
        return 1;
    }
    if (decoded_packet.type != HOPE_PACKET_TYPE_COMMAND ||
        command_protocol_parse_request_payload(decoded_packet.payload, decoded_packet.payload_len, &decoded_request) != ESP_OK ||
        decoded_request.command_id != 44 ||
        decoded_request.opcode != COMMAND_OPCODE_PING ||
        decoded_request.auth_key_id != 0) {
        printf("FAIL: command request decode mismatch\n");
        return 1;
    }

    command_ack_t ack = {
        .version = COMMAND_PAYLOAD_VERSION,
        .acked_type = HOPE_PACKET_TYPE_COMMAND,
        .command_id = 44,
        .status = COMMAND_ACK_STATUS_OK,
        .detail_code = 0,
        .message_len = 4,
        .message = "pong",
    };
    hope_packet_t ack_packet;
    command_ack_t decoded_ack;
    if (command_protocol_build_ack_packet(&ack, 1, 2, 0x12345678, 45, 100, &ack_packet) != ESP_OK ||
        command_protocol_parse_ack_payload(ack_packet.payload, ack_packet.payload_len, &decoded_ack) != ESP_OK ||
        decoded_ack.command_id != 44 ||
        decoded_ack.status != COMMAND_ACK_STATUS_OK ||
        strcmp(decoded_ack.message, "pong") != 0) {
        printf("FAIL: command ACK decode mismatch\n");
        return 1;
    }

    printf("PASS: command protocol\n");
    return 0;
}

int main(void) {
    int failures = 0;

    failures += test_packet_encode_decode();
    failures += test_session_counter();
    failures += test_malformed_packet_rejection();
    failures += test_replay_protection();
    failures += test_gnss_parser();
    failures += test_command_protocol();

    if (failures == 0) {
        printf("ALL TESTS PASS\n");
        return 0;
    }

    printf("%d TEST GROUP(S) FAILED\n", failures);
    return 1;
}
