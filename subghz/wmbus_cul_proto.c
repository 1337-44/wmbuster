#include "wmbus_cul_proto.h"
#include "../protocol/wmbus_crc.h"

#include <string.h>

/* CUL sends a hex line containing a full CRC-protected wM-Bus frame.
 * With X21, final two bytes are LQI and raw CC1101 RSSI. */
#define CUL_MAX_RAW 294

static int hex_digit(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

WmbusCulParseResult wmbus_cul_parse_line(
    const char* line, uint8_t* out, size_t capacity,
    size_t* out_len, int8_t* rssi) {
    if(out_len) *out_len = 0;
    if(rssi) *rssi = -127;
    if(!line || !out || !out_len || !rssi || capacity < 11) {
        return WmbusCulParseBad;
    }
    if(line[0] != 'b') return WmbusCulParseIgnore;

    bool format_b = line[1] == 'Y';
    const char* s = line + (format_b ? 2 : 1);
    if(hex_digit(*s) < 0) return WmbusCulParseIgnore; /* non-frame CUL text */

    size_t hexlen = strlen(s);
    if((hexlen & 1) || hexlen < 24 || hexlen > 2 * CUL_MAX_RAW)
        return WmbusCulParseBad;

    uint8_t raw[CUL_MAX_RAW];
    size_t n = hexlen / 2;
    for(size_t i = 0; i < n; i++) {
        int hi = hex_digit(s[i * 2]);
        int lo = hex_digit(s[i * 2 + 1]);
        if(hi < 0 || lo < 0) return WmbusCulParseBad;
        raw[i] = (uint8_t)((hi << 4) | lo);
    }

    size_t L = raw[0];
    if(L < 9 || L + 1 > capacity) return WmbusCulParseBad;

    size_t expected = L + 1;
    if(format_b) {
        /* Format B: CRC at tail, L includes the two CRC bytes. */
        if(L < 12) return WmbusCulParseBad;
    } else {
        /* Format A: first 10 data bytes + CRC and a CRC per next
         * block of up to 16 bytes. L excludes all CRC bytes. */
        size_t following = L - 9;
        size_t blocks = 1 + (following + 15) / 16;
        expected += blocks * 2;
    }

    if(n == expected + 2) {
        /* X21 appends LQI, then CC1101 RSSI raw byte. CC1101 datasheet:
         * RSSI in dBm = signed_raw/2 - RSSI_OFFSET (868 MHz ~74 dB). */
        int signed_rssi = raw[n - 1];
        if(signed_rssi >= 128) signed_rssi -= 256;
        int dbm = signed_rssi / 2 - 74;
        if(dbm < -127) dbm = -127;
        if(dbm > 127) dbm = 127;
        *rssi = (int8_t)dbm;
    } else if(n != expected) {
        return WmbusCulParseBad;
    }

    size_t decoded_len = 0;
    if(format_b) {
        if(!wmbus_crc_verify_format_b(raw, expected)) return WmbusCulParseBad;
        decoded_len = expected - 2;
        if(decoded_len > capacity) return WmbusCulParseBad;
        memcpy(out, raw, decoded_len);
        /* Format B L counts its trailing CRC. After stripping CRC the
         * normalised link frame must have L == payload length - 1. */
        out[0] = (uint8_t)(decoded_len - 1);
    } else {
        if(!wmbus_crc_verify_format_a(raw, expected, out, capacity, &decoded_len)) {
            return WmbusCulParseBad;
        }
    }
    if(decoded_len < 11) return WmbusCulParseBad;
    *out_len = decoded_len;
    return format_b ? WmbusCulParseFrameB : WmbusCulParseFrameA;
}
