/* Flipper Zero UART GNSS receiver.
 * UART transport adapted from ezod/flipperzero-gps (GPL-3.0),
 * https://github.com/ezod/flipperzero-gps
 * NMEA parser by Kosma Moczek (minmea, WTFPL 2),
 * https://github.com/kosma/minmea (see gps/COPYING.minmea).
 *
 * Distinguishes RMC validity from GGA metadata, rejects invalid checksums
 * and stale fixes, never holds the GPS mutex during SD/radio operations. */
#include "gps_receiver.h"
#include "minmea.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_power.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GPS_EVENT_RX   (1UL << 0)
#define GPS_EVENT_STOP (1UL << 1)
#define GPS_BAUD 9600
#define GPS_FIX_MAX_AGE_MS 5000
#define GPS_GGA_MAX_AGE_MS 10000
#define GPS_SENTENCE_CAP 128

struct WmbusGpsReceiver {
    FuriMutex* lock;
    FuriThread* thread;
    FuriStreamBuffer* rx_stream;
    FuriHalSerialHandle* serial;
    bool otg_enabled_by_gps; /* restore preexisting 5 V GPIO state on stop */
    WmbusGpsFix sample;
    bool rmc_valid;
    bool have_gga;
    uint32_t rmc_tick;
    uint32_t gga_tick;
};

static void gps_parse_line(WmbusGpsReceiver* g, const char* line) {
    /* Strict checksum verification avoids false positions from noisy UART. */
    enum minmea_sentence_id type = minmea_sentence_id(line, true);
    if(type == MINMEA_SENTENCE_RMC) {
        struct minmea_sentence_rmc r;
        if(!minmea_parse_rmc(&r, line)) return;
        furi_mutex_acquire(g->lock, FuriWaitForever);
        g->rmc_valid = r.valid;
        g->rmc_tick = furi_get_tick();
        if(r.valid) {
            float lat = minmea_tocoord(&r.latitude);
            float lon = minmea_tocoord(&r.longitude);
            float speed = minmea_tofloat(&r.speed);
            float course = minmea_tofloat(&r.course);
            if(!isfinite(lat) || !isfinite(lon) ||
                lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f) {
                g->rmc_valid = false;
            } else {
                g->sample.latitude = lat;
                g->sample.longitude = lon;
                g->sample.speed_valid = isfinite(speed) && speed >= 0;
                g->sample.course_valid = isfinite(course) && course >= 0;
                g->sample.speed_knots = g->sample.speed_valid ? speed : 0.0f;
                g->sample.speed_kmh = g->sample.speed_knots * 1.852f;
                g->sample.course_deg = g->sample.course_valid ? course : 0.0f;
                int y = r.date.year;
                if(y >= 0 && y < 80) y += 2000;
                else if(y >= 80 && y < 100) y += 1900;
                g->sample.utc_valid =
                    y >= 2000 && y <= 2099 &&
                    r.date.month >= 1 && r.date.month <= 12 &&
                    r.date.day >= 1 && r.date.day <= 31 &&
                    r.time.hours >= 0 && r.time.hours < 24 &&
                    r.time.minutes >= 0 && r.time.minutes < 60 &&
                    r.time.seconds >= 0 && r.time.seconds < 60;
                if(g->sample.utc_valid) {
                    g->sample.year = (uint16_t)y;
                    g->sample.month = (uint8_t)r.date.month;
                    g->sample.day = (uint8_t)r.date.day;
                    g->sample.hour = (uint8_t)r.time.hours;
                    g->sample.minute = (uint8_t)r.time.minutes;
                    g->sample.second = (uint8_t)r.time.seconds;
                }
            }
        }
        furi_mutex_release(g->lock);
    } else if(type == MINMEA_SENTENCE_GGA) {
        struct minmea_sentence_gga a;
        if(!minmea_parse_gga(&a, line)) return;
        furi_mutex_acquire(g->lock, FuriWaitForever);
        g->have_gga = a.fix_quality > 0;
        g->gga_tick = furi_get_tick();
        if(g->have_gga) {
            float alt = minmea_tofloat(&a.altitude);
            float hdop = minmea_tofloat(&a.hdop);
            g->sample.altitude_valid = isfinite(alt);
            g->sample.hdop_valid = isfinite(hdop);
            g->sample.altitude_m = g->sample.altitude_valid ? alt : 0.0f;
            g->sample.hdop = g->sample.hdop_valid ? hdop : 0.0f;
            g->sample.satellites = (uint8_t)
                (a.satellites_tracked > 0 && a.satellites_tracked <= 99 ?
                 a.satellites_tracked : 0);
            g->sample.fix_quality = (uint8_t)a.fix_quality;
        }
        furi_mutex_release(g->lock);
    }
}

static void gps_serial_callback(
    FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    WmbusGpsReceiver* g = context;
    if(event != FuriHalSerialRxEventData) return;
    uint8_t data = furi_hal_serial_async_rx(handle);
    furi_stream_buffer_send(g->rx_stream, &data, 1, 0);
    furi_thread_flags_set(furi_thread_get_id(g->thread), GPS_EVENT_RX);
}

static int32_t gps_worker(void* context) {
    WmbusGpsReceiver* g = context;
    char line[GPS_SENTENCE_CAP];
    size_t n = 0;
    for(;;) {
        uint32_t events = furi_thread_flags_wait(
            GPS_EVENT_RX | GPS_EVENT_STOP, FuriFlagWaitAny, FuriWaitForever);
        if(events & GPS_EVENT_STOP) break;
        if(!(events & GPS_EVENT_RX)) continue;
        uint8_t b;
        while(furi_stream_buffer_receive(g->rx_stream, &b, 1, 0) == 1) {
            if(b == '$') {
                n = 0;
                line[n++] = '$';
            } else if(n > 0) {
                if(b == '\n') {
                    while(n > 0 && line[n - 1] == '\r') n--;
                    line[n] = 0;
                    gps_parse_line(g, line);
                    n = 0;
                } else if(n < sizeof(line) - 1) {
                    line[n++] = (char)b;
                } else {
                    n = 0; /* overflow: discard until a new '$' */
                }
            }
        }
    }
    return 0;
}

WmbusGpsReceiver* wmbus_gps_start(void) {
    WmbusGpsReceiver* g = malloc(sizeof(*g));
    if(!g) return NULL;
    memset(g, 0, sizeof(*g));
    g->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!g->serial) {
        free(g);
        return NULL;
    }
    g->lock = furi_mutex_alloc(FuriMutexTypeNormal);
    g->rx_stream = furi_stream_buffer_alloc(1024, 1);
    g->thread = furi_thread_alloc();
    if(!g->lock || !g->rx_stream || !g->thread) {
        if(g->thread) furi_thread_free(g->thread);
        if(g->rx_stream) furi_stream_buffer_free(g->rx_stream);
        if(g->lock) furi_mutex_free(g->lock);
        furi_hal_serial_control_release(g->serial);
        free(g);
        return NULL;
    }
    /* Many NEO-6M breakout boards are powered from GPIO pin 1 (5 V).
     * Unlike the 3.3 V rail, this supply is off by default.
     * Preserve an already-enabled 5 V supply: only undo our own enable. */
    if(!furi_hal_power_is_otg_enabled()) {
        for(uint8_t i = 0; i < 5; i++) {
            if(furi_hal_power_enable_otg()) {
                g->otg_enabled_by_gps = true;
                break;
            }
            furi_delay_ms(50);
        }
        if(!g->otg_enabled_by_gps) {
            FURI_LOG_W("WmbusGPS", "Could not enable 5V on GPIO pin 1");
        }
    }

    furi_thread_set_name(g->thread, "WmbusGPS");
    furi_thread_set_stack_size(g->thread, 2048);
    furi_thread_set_context(g->thread, g);
    furi_thread_set_callback(g->thread, gps_worker);
    furi_thread_start(g->thread);
    furi_hal_serial_init(g->serial, GPS_BAUD);
    furi_hal_serial_async_rx_start(g->serial, gps_serial_callback, g, false);
    return g;
}

void wmbus_gps_stop(WmbusGpsReceiver* g) {
    if(!g) return;
    furi_hal_serial_async_rx_stop(g->serial);
    furi_thread_flags_set(furi_thread_get_id(g->thread), GPS_EVENT_STOP);
    furi_thread_join(g->thread);
    furi_hal_serial_deinit(g->serial);
    furi_hal_serial_control_release(g->serial);
    /* Never turn off 5 V if it was enabled before wM-Buster started GPS. */
    if(g->otg_enabled_by_gps && furi_hal_power_is_otg_enabled()) {
        furi_hal_power_disable_otg();
    }
    furi_thread_free(g->thread);
    furi_stream_buffer_free(g->rx_stream);
    furi_mutex_free(g->lock);
    free(g);
}

bool wmbus_gps_snapshot(WmbusGpsReceiver* g, WmbusGpsFix* out) {
    if(!out) return false;
    memset(out, 0, sizeof(*out));
    if(!g) return false;
    if(furi_mutex_acquire(g->lock, 20) != FuriStatusOk) return false;
    *out = g->sample;
    uint32_t now = furi_get_tick();
    out->age_ms = (uint32_t)(now - g->rmc_tick);
    out->fix = g->rmc_valid && out->utc_valid &&
        out->age_ms <= GPS_FIX_MAX_AGE_MS;
    out->gga_valid = g->have_gga &&
        (uint32_t)(now - g->gga_tick) <= GPS_GGA_MAX_AGE_MS;
    if(!out->fix) {
        out->latitude = 0.0f;
        out->longitude = 0.0f;
        out->speed_kmh = 0.0f;
        out->speed_knots = 0.0f;
        out->course_deg = 0.0f;
        out->utc_valid = false;
    }
    furi_mutex_release(g->lock);
    return true;
}
