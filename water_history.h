#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Small per-meter fixed ring (~200 B); no dynamic memory in RX worker.
 * Values are integer litres to avoid float rounding and false leaks. */
#define WMBUS_WATER_POINTS 24

typedef struct {
    uint32_t litres[WMBUS_WATER_POINTS];
    uint32_t ticks[WMBUS_WATER_POINTS];
    uint8_t count;
    uint8_t next;
    uint32_t last_litres;
    uint32_t last_increase_tick;
    uint32_t chain_start_tick;
    uint32_t chain_litres;
    uint8_t chain_events;
    bool valid;
    bool leak_active;
} WmbusWaterHistory;

/* Only a decoded row with 'Now' or 'Total' AND 'm3' is accepted. */
bool wmbus_water_parse(const char* decoded_text, uint32_t* litres);

/* Returns true only when the leak condition first becomes active.
 * Gaps, reset/rollover, anomalous jumps and unchanging reads do not
 * independently trigger an alarm. */
bool wmbus_water_update(
    WmbusWaterHistory* h, uint32_t litres, uint32_t now_ms,
    uint32_t window_minutes, uint32_t minimum_litres);

uint32_t wmbus_water_rate_lph(const WmbusWaterHistory* h);
bool wmbus_water_recent_flow(const WmbusWaterHistory* h, uint32_t now_ms);
