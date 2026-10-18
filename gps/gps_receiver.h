#pragma once
#include <stdbool.h>
#include <stdint.h>

/* NMEA data from an external UART GPS; positions are receiver locations,
 * NOT meter locations. No-fix/stale data must never be logged as coordinates. */
typedef struct {
    bool fix;
    bool utc_valid;
    bool gga_valid;
    bool speed_valid;
    bool course_valid;
    bool altitude_valid;
    bool hdop_valid;
    float latitude;
    float longitude;
    float speed_knots;
    float speed_kmh;
    float course_deg;
    float altitude_m;
    float hdop;
    uint8_t satellites;
    uint8_t fix_quality;
    uint32_t age_ms;
    uint16_t year;
    uint8_t month, day, hour, minute, second;
} WmbusGpsFix;

typedef struct WmbusGpsReceiver WmbusGpsReceiver;

/* Uses Flipper USART pins (RX 14 / TX 13), baud 9600. Returns NULL when
 * already occupied; do not crash or disturb CC1101 reception. */
WmbusGpsReceiver* wmbus_gps_start(void);
void wmbus_gps_stop(WmbusGpsReceiver* gps);

/* Copy coherent snapshot; returns false on unavailable receiver/lock.
 * Fix becomes false if RMC says invalid or a sentence is stale (>5 sec). */
bool wmbus_gps_snapshot(WmbusGpsReceiver* gps, WmbusGpsFix* out);
