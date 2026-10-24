#pragma once
#include <stdbool.h>
#include <stdint.h>

/* NMEA data from an external UART GPS; positions are receiver locations,
 * NOT meter locations. No-fix/stale data must never be logged as coordinates. */
typedef struct {
    bool fix;
    bool utc_valid;
    bool gga_valid;
    bool uart_data_seen; /* recent raw bytes from GPS UART */
    bool nmea_seen;      /* recent successfully parsed RMC or GGA */
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

/* Snapshot: position valid with fresh RMC or GGA (quality >0).
 * UTC date is separately validated; never manufacture RMC UTC from GGA.
 * Also exposes recent UART byte and valid RMC/GGA activity. */
bool wmbus_gps_snapshot(WmbusGpsReceiver* gps, WmbusGpsFix* out);
