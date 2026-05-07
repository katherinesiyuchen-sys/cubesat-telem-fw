#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "packet.h"
#include "packet_codec.h"
#include "command_protocol.h"
#include "lattice_protocol.h"
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

    const char *valid_gga = "$GNGGA,123519,3748.2900,N,12216.3800,W,1,08,0.9,10.0,M,0.0,M,,*4D";
    if (gnss_parse_sentence(valid_gga, &fix) != ESP_OK || !fix.valid) {
        printf("FAIL: GNSS parser rejected valid GGA\n");
        return 1;
    }

    if (fix.latitude_e7 != 378048333 ||
        fix.longitude_e7 != -1222730000 ||
        fix.satellites != 8 ||
        fix.hdop_x100 != 90 ||
        fix.altitude_m_x10 != 100 ||
        (fix.source_flags & GNSS_FIX_FLAG_CHECKSUM) == 0) {
        printf(
            "FAIL: GNSS parser produced unexpected GGA fix: lat=%ld lon=%ld sats=%u hdop=%u alt=%ld flags=0x%02X\n",
            (long)fix.latitude_e7,
            (long)fix.longitude_e7,
            (unsigned)fix.satellites,
            (unsigned)fix.hdop_x100,
            (long)fix.altitude_m_x10,
            (unsigned)fix.source_flags
        );
        return 1;
    }

    const char *bad_checksum_gga = "$GNGGA,123519,3748.2900,N,12216.3800,W,1,08,0.9,10.0,M,0.0,M,,*00";
    if (gnss_parse_sentence(bad_checksum_gga, &fix) != ESP_ERR_INVALID_CRC) {
        printf("FAIL: GNSS parser accepted bad checksum GGA\n");
        return 1;
    }

    const char *no_fix_gga = "$GNGGA,123519,3748.2900,N,12216.3800,W,0,00,0.9,10.0,M,0.0,M,,*44";
    if (gnss_parse_sentence(no_fix_gga, &fix) == ESP_OK) {
        printf("FAIL: GNSS parser accepted no-fix GGA\n");
        return 1;
    }

    const char *valid_rmc = "$GNRMC,123519,A,3748.2900,N,12216.3800,W,12.5,270.1,010126,,,A*49";
    if (gnss_parse_sentence(valid_rmc, &fix) != ESP_OK || !fix.valid) {
        printf("FAIL: GNSS parser rejected valid RMC\n");
        return 1;
    }

    if (fix.speed_mps_x100 != 643 ||
        fix.course_deg_x100 != 27010 ||
        fix.utc_time_ms != 45319000 ||
        fix.utc_date_ddmmyy != 10126 ||
        (fix.source_flags & GNSS_FIX_FLAG_RMC) == 0) {
        printf(
            "FAIL: GNSS parser produced unexpected RMC fix: speed=%u course=%u time=%lu date=%lu flags=0x%02X\n",
            (unsigned)fix.speed_mps_x100,
            (unsigned)fix.course_deg_x100,
            (unsigned long)fix.utc_time_ms,
            (unsigned long)fix.utc_date_ddmmyy,
            (unsigned)fix.source_flags
        );
        return 1;
    }

    const char *no_fix_rmc = "$GNRMC,123519,V,3748.2900,N,12216.3800,W,0.0,0.0,010126,,,N*63";
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

static int test_lattice_protocol(void) {
    uint8_t object[800];
    uint8_t payload[HOPE_MAX_PAYLOAD_LEN];
    lattice_reassembly_t reassembly;

    for (size_t i = 0; i < sizeof(object); ++i) {
        object[i] = (uint8_t)((i * 17U) & 0xFFU);
    }

    uint16_t fragments = lattice_protocol_fragment_count(sizeof(object));
    if (fragments <= 1) {
        printf("FAIL: lattice fragment count too small\n");
        return 1;
    }

    lattice_reassembly_reset(&reassembly);
    for (uint16_t index = 0; index < fragments; ++index) {
        size_t written = 0;
        lattice_fragment_t fragment;
        bool complete = false;

        if (lattice_protocol_build_fragment_payload(
                LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY,
                0x44,
                index,
                object,
                sizeof(object),
                payload,
                sizeof(payload),
                &written
            ) != ESP_OK) {
            printf("FAIL: lattice fragment build failed\n");
            return 1;
        }

        if (lattice_protocol_parse_fragment_payload(payload, written, &fragment) != ESP_OK) {
            printf("FAIL: lattice fragment parse failed\n");
            return 1;
        }

        if (lattice_reassembly_add(&reassembly, &fragment, &complete) != ESP_OK) {
            printf("FAIL: lattice fragment reassembly failed\n");
            return 1;
        }

        if ((index + 1U == fragments) != complete) {
            printf("FAIL: lattice completion state mismatch\n");
            return 1;
        }
    }

    if (lattice_reassembly_len(&reassembly) != sizeof(object) ||
        memcmp(lattice_reassembly_data(&reassembly), object, sizeof(object)) != 0) {
        printf("FAIL: lattice reassembled object mismatch\n");
        return 1;
    }

    printf("PASS: lattice protocol\n");
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
    failures += test_lattice_protocol();

    if (failures == 0) {
        printf("ALL TESTS PASS\n");
        return 0;
    }

    printf("%d TEST GROUP(S) FAILED\n", failures);
    return 1;
}
