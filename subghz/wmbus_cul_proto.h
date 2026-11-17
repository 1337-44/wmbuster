#pragma once
/* Pure, no-HAL parser for culfw serial wM-Bus frames.
 * See https://wmbusmeters.github.io/wmbusmeters-wiki/nanoCUL.html */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    WmbusCulParseIgnore = 0,
    WmbusCulParseFrameA,
    WmbusCulParseFrameB,
    WmbusCulParseBad,
} WmbusCulParseResult;

/* Input line: b<hex frame A>[LQI RSSI] or bY<hex frame B>[LQI RSSI].
 * Exact byte lengths are checked from the link L field; extra bytes
 * other than the two optional X21 metadata bytes are rejected.
 * All link CRCs must pass. out receives CRC-STRIPPED data starting at L.
 * RSSI is reported in approximate dBm (-127 if no X21 suffix).
 */
WmbusCulParseResult wmbus_cul_parse_line(
    const char* line,
    uint8_t* out,
    size_t capacity,
    size_t* out_len,
    int8_t* rssi);
