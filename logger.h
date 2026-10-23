#pragma once
#include <stdint.h>
#include <stddef.h>
#include <storage/storage.h>
#include "protocol/wmbus_link.h"
#include "gps/gps_receiver.h"

/* Append RAW telegram hex, link-layer JSON and parsed text to a session log
 * under WMBUS_APP_LOG_DIR. File name is local Flipper RTC time when valid.
 * Each JSON record includes local device time and optional fresh GPS fix.
 * Returns true only if the frame and JSON were written successfully. */
bool wmbus_log_telegram(
    Storage* storage,
    const uint8_t* frame, size_t len,
    int8_t rssi,
    const WmbusLinkFrame* link,
    const char* parsed_text,
    const WmbusGpsFix* gps,
    bool gps_enabled);
