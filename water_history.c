#include "water_history.h"
#include <limits.h>
#include <string.h>

static bool read_value(const char* p, uint32_t* out) {
    while(*p == ' ' || *p == '\t') ++p;
    if(*p < '0' || *p > '9') return false;
    uint32_t units = 0, fraction = 0;
    unsigned digits = 0;
    while(*p >= '0' && *p <= '9') {
        uint8_t d = (uint8_t)(*p++ - '0');
        if(units > (UINT32_MAX / 1000 - d) / 10) return false;
        units = units * 10 + d;
    }
    if(*p == '.') {
        p++;
        while(*p >= '0' && *p <= '9') {
            if(digits >= 3) return false;
            fraction = fraction * 10 + (uint8_t)(*p++ - '0');
            digits++;
        }
    }
    while(digits++ < 3) fraction *= 10;
    while(*p == ' ' || *p == '\t') ++p;
    if(p[0] != 'm' || p[1] != '3') return false;
    p += 2;
    if(*p && *p != '\r' && *p != '\n' && *p != ' ' && *p != '\t') return false;
    if(units > (UINT32_MAX - fraction) / 1000) return false;
    *out = units * 1000 + fraction;
    return true;
}

bool wmbus_water_parse(const char* text, uint32_t* litres) {
    if(!text || !litres) return false;
    const char* p = text;
    while(*p) {
        if((strncmp(p, "Now", 3) == 0 && (p[3] == ' ' || p[3] == '\t')) ||
           (strncmp(p, "Total", 5) == 0 && (p[5] == ' ' || p[5] == '\t'))) {
            const char* v = p + (p[0] == 'N' ? 3 : 5);
            if(read_value(v, litres)) return true;
        }
        while(*p && *p != '\n') p++;
        if(*p == '\n') p++;
    }
    return false;
}

static uint32_t raw_rate(const WmbusWaterHistory* h);

static void add_sample(WmbusWaterHistory* h, uint32_t litres, uint32_t tick) {
    h->litres[h->next] = litres;
    h->ticks[h->next] = tick;
    h->next = (uint8_t)((h->next + 1) % WMBUS_WATER_POINTS);
    if(h->count < WMBUS_WATER_POINTS) h->count++;
    if(h->count >= 2) {
        uint32_t raw = raw_rate(h);
        if(!h->rate_primed) {
            h->stable_lph = raw;
            h->rate_primed = true;
        } else {
            h->stable_lph = (uint32_t)(((uint64_t)h->stable_lph * 7 +
                                     (uint64_t)raw * 3 + 5) / 10);
        }
    }
}

bool wmbus_water_update(
    WmbusWaterHistory* h, uint32_t litres, uint32_t now_ms,
    uint32_t window_minutes, uint32_t minimum_litres) {
    if(!h) return false;
    if(window_minutes < 1 || window_minutes > 120) window_minutes = 30;
    if(minimum_litres < 1) minimum_litres = 1;
    uint32_t window = window_minutes * 60000UL;
    uint32_t allowed_gap = window / 2;
    if(allowed_gap < 120000UL) allowed_gap = 120000UL;
    if(allowed_gap > 900000UL) allowed_gap = 900000UL;
    if(!h->valid) {
        h->valid = true;
        h->last_litres = litres;
        add_sample(h, litres, now_ms);
        return false;
    }
    /* Meter reset/rollover, too long a reception outage or a suspicious
     * enormous single jump breaks the continuity of the sequence. */
    uint32_t previous_tick = h->ticks[(h->next + WMBUS_WATER_POINTS - 1) % WMBUS_WATER_POINTS];
    bool stale = (uint32_t)(now_ms - previous_tick) > window;
    bool reset = litres < h->last_litres;
    uint32_t inc = reset ? 0 : litres - h->last_litres;
    if(stale || reset || inc > 10000) {
        h->chain_events = 0;
        h->chain_litres = 0;
        h->chain_start_tick = 0;
        h->last_increase_tick = 0;
        h->leak_active = false;
        if(reset || inc > 10000) {
            h->count = h->next = 0;
            h->stable_lph = 0;
            h->rate_primed = false;
        }
    }
    if(!reset && inc > 0 && inc <= 10000) {
        if(h->chain_events == 0 || h->last_increase_tick == 0 ||
           (uint32_t)(now_ms - h->last_increase_tick) > allowed_gap) {
            h->chain_events = 0;
            h->chain_litres = 0;
            h->chain_start_tick = now_ms;
            h->leak_active = false;
        }
        h->last_increase_tick = now_ms;
        if(h->chain_events < UINT8_MAX) h->chain_events++;
        if(UINT32_MAX - h->chain_litres >= inc) h->chain_litres += inc;
    } else if(h->last_increase_tick &&
              (uint32_t)(now_ms - h->last_increase_tick) > allowed_gap) {
        h->chain_events = 0;
        h->chain_litres = 0;
        h->chain_start_tick = 0;
        h->leak_active = false;
    }
    h->last_litres = litres;
    add_sample(h, litres, now_ms);
    if(!h->leak_active && h->chain_events >= 3 &&
       h->chain_litres >= minimum_litres &&
       (uint32_t)(now_ms - h->chain_start_tick) >= window) {
        h->leak_active = true;
        return true;
    }
    return false;
}

static uint32_t raw_rate(const WmbusWaterHistory* h) {
    if(!h || h->count < 2) return 0;
    uint8_t newest = (uint8_t)((h->next + WMBUS_WATER_POINTS - 1) % WMBUS_WATER_POINTS);
    uint8_t oldest = h->count == WMBUS_WATER_POINTS ? h->next : 0;
    uint32_t elapsed = (uint32_t)(h->ticks[newest] - h->ticks[oldest]);
    if(elapsed < 10000 || h->litres[newest] < h->litres[oldest]) return 0;
    uint64_t scaled = (uint64_t)(h->litres[newest] - h->litres[oldest]) * 3600000ULL;
    uint64_t rate = scaled / elapsed;
    return rate > UINT32_MAX ? UINT32_MAX : (uint32_t)rate;
}

uint32_t wmbus_water_rate_lph(const WmbusWaterHistory* h) {
    return h && h->rate_primed ? h->stable_lph : 0;
}

bool wmbus_water_recent_flow(const WmbusWaterHistory* h, uint32_t now_ms) {
    return h && h->valid && h->last_increase_tick &&
           (uint32_t)(now_ms - h->last_increase_tick) < 20000UL;
}
