/* SD logs: raw telegrams, decoded values, RTC time and optional GPS fix.
 * No GPS coordinates are written unless a recent, valid RMC is available. */
#include "logger.h"
#include "wmbus_app_i.h"
#include "protocol/wmbus_manuf.h"
#include <furi.h>
#include <furi_hal_rtc.h>
#include <stdio.h>
#include <string.h>

static void write_str(File* f, const char* s) {
    storage_file_write(f, s, strlen(s));
}

/* The session file is named when the first telegram is received, using the
 * Flipper RTC (local device time). Prevent overwriting or accidental
 * appending to another session with the same second via numeric suffix. */
static void init_session_path(Storage* storage, char* path, size_t cap) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    char stem[72];
    if(dt.year >= 2020 && dt.year <= 2099 && dt.month >= 1 && dt.month <= 12 &&
        dt.day >= 1 && dt.day <= 31 && dt.hour < 24 && dt.minute < 60 && dt.second < 60) {
        snprintf(stem, sizeof(stem), "session_%04u-%02u-%02u_%02u-%02u-%02u",
            (unsigned)dt.year, (unsigned)dt.month, (unsigned)dt.day,
            (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second);
    } else {
        /* RTC missing: only then use monotonic ticks, explicitly labelled boot. */
        snprintf(stem, sizeof(stem), "session_boot_%lu", (unsigned long)furi_get_tick());
    }
    for(unsigned n = 0; n < 1000; n++) {
        if(n == 0) snprintf(path, cap, "%s/%s.log", WMBUS_APP_LOG_DIR, stem);
        else snprintf(path, cap, "%s/%s_%03u.log", WMBUS_APP_LOG_DIR, stem, n);
        if(!storage_file_exists(storage, path)) return;
    }
    snprintf(path, cap, "%s/session_boot_%lu.log",
             WMBUS_APP_LOG_DIR, (unsigned long)furi_get_tick());
}

static bool is_leap(unsigned y) {
    return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
}
static unsigned month_days(unsigned y, unsigned m) {
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return m == 2 && is_leap(y) ? 29 : days[m - 1];
}

/* RMC UTC represents the last receiver sample. Advance by tick age to
 * approximate the UTC second when the radio telegram was handled.
 * Rollover-safe for midnight and month/year boundaries. */
static void gps_telegram_time(const WmbusGpsFix* gps, char* dest, size_t cap) {
    unsigned y = gps->year, mo = gps->month, day = gps->day;
    unsigned h = gps->hour, mi = gps->minute, sec = gps->second;
    unsigned remaining = gps->age_ms / 1000;
    while(remaining--) {
        if(++sec >= 60) {
            sec = 0;
            if(++mi >= 60) {
                mi = 0;
                if(++h >= 24) {
                    h = 0;
                    if(++day > month_days(y, mo)) {
                        day = 1;
                        if(++mo > 12) { mo = 1; y++; }
                    }
                }
            }
        }
    }
    snprintf(dest, cap, "%04u-%02u-%02uT%02u:%02u:%02uZ",
             y, mo, day, h, mi, sec);
}

bool wmbus_log_telegram(
    Storage* storage, const uint8_t* frame, size_t len, int8_t rssi,
    const WmbusLinkFrame* link, const char* parsed_text,
    const WmbusGpsFix* gps, bool gps_enabled) {
    storage_simply_mkdir(storage, WMBUS_APP_DATA_DIR);
    storage_simply_mkdir(storage, WMBUS_APP_LOG_DIR);
    static char path[120];
    static bool initialized = false;
    if(!initialized) {
        init_session_path(storage, path, sizeof(path));
        initialized = true;
    }

    File* f = storage_file_alloc(storage);
    if(!storage_file_open(f, path, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        storage_file_free(f);
        return false;
    }

    uint32_t tick = furi_get_tick();
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "RAW %lu %d ", (unsigned long)tick, (int)rssi);
    write_str(f, hdr);
    char hx[3];
    for(size_t i = 0; i < len; i++) {
        snprintf(hx, sizeof(hx), "%02X", frame[i]);
        storage_file_write(f, hx, 2);
    }
    write_str(f, "\n");

    /* Device clock (local time; timezone offset unknown) is included
     * even without GPS. The GPS UTC timestamp is independently sourced. */
    DateTime rtc;
    furi_hal_rtc_get_datetime(&rtc);
    char m[4];
    wmbus_manuf_decode(link->M, m);
    char json[640];
    int n = snprintf(json, sizeof(json),
        "JSON {\"manuf\":\"%s\",\"id\":\"%08lX\",\"ver\":%u,\"med\":%u,"
        "\"c\":%u,\"ci\":%u,\"enc\":%u,\"rssi\":%d,\"tick_ms\":%lu,"
        "\"device_time\":\"%04u-%02u-%02uT%02u:%02u:%02u\","
        "\"gps_enabled\":%s,\"gps\":",
        m, (unsigned long)link->id, link->version, link->medium,
        link->C, link->ci, link->enc_mode, (int)rssi, (unsigned long)tick,
        (unsigned)rtc.year, (unsigned)rtc.month, (unsigned)rtc.day,
        (unsigned)rtc.hour, (unsigned)rtc.minute, (unsigned)rtc.second,
        gps_enabled ? "true" : "false");
    if(n < 0 || (size_t)n >= sizeof(json)) {
        storage_file_close(f);
        storage_file_free(f);
        return false;
    }
    size_t used = (size_t)n;
    if(gps && gps->fix) {
        char utc[32] = "";
        if(gps->utc_valid) gps_telegram_time(gps, utc, sizeof(utc));
        n = snprintf(json + used, sizeof(json) - used,
            "{\"fix\":true,\"utc\":%s",
            gps->utc_valid ? "\"" : "null");
        /* Replace the temporary opening quote with a full UTC field. */
        if(n < 0 || (size_t)n >= sizeof(json) - used) goto truncated;
        used += (size_t)n;
        if(gps->utc_valid) {
            n = snprintf(json + used, sizeof(json) - used,
                         "%s\"", utc);
            if(n < 0 || (size_t)n >= sizeof(json) - used) goto truncated;
            used += (size_t)n;
        }
        n = snprintf(json + used, sizeof(json) - used,
            ",\"lat\":%.7f,\"lon\":%.7f,\"speed_kmh\":%.3f,"
            "\"speed_knots\":%.3f,\"course_deg\":%.2f,"
            "\"age_ms\":%lu",
            gps->latitude, gps->longitude, gps->speed_kmh,
            gps->speed_knots, gps->course_deg, (unsigned long)gps->age_ms);
        if(n < 0 || (size_t)n >= sizeof(json) - used) goto truncated;
        used += (size_t)n;
        if(gps->gga_valid) {
            n = snprintf(json + used, sizeof(json) - used,
                ",\"fix_quality\":%u,\"satellites\":%u,\"hdop\":%.2f,"
                "\"altitude_m\":%.2f",
                (unsigned)gps->fix_quality, (unsigned)gps->satellites,
                gps->hdop, gps->altitude_m);
            if(n < 0 || (size_t)n >= sizeof(json) - used) goto truncated;
            used += (size_t)n;
        }
        n = snprintf(json + used, sizeof(json) - used, "}}\n");
    } else {
        n = snprintf(json + used, sizeof(json) - used, "null}\n");
    }
    if(n < 0 || (size_t)n >= sizeof(json) - used) goto truncated;
    used += (size_t)n;
    if(storage_file_write(f, json, used) != used) goto truncated;

    if(parsed_text && *parsed_text) {
        write_str(f, "TXT ");
        write_str(f, parsed_text);
        if(parsed_text[strlen(parsed_text) - 1] != '\n') write_str(f, "\n");
    }
    storage_file_close(f);
    storage_file_free(f);
    return true;

truncated:
    storage_file_close(f);
    storage_file_free(f);
    return false;
}
