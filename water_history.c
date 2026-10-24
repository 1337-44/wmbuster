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

/* Alert conservatively. A 1 L counter step does NOT mean 1 L of
 * continuous flow; ordinary activities produce exactly that pattern.
 *
 * A candidate must show many small increments distributed across every
 * quarter of the observation, while remaining below a 12 L/h long-term
 * ceiling AND without a 5 L/5 min normal-use burst. */
void wmbus_water_reset_leak(WmbusWaterHistory* h) {
    if(!h) return;
    h->chain_events = 0;
    h->chain_litres = 0;
    h->chain_start_tick = 0;
    h->last_increase_tick = 0;
    h->leak_quarters = 0;
    h->leak_active = false;
    h->burst_start_tick = 0;
    h->burst_litres = 0;
}

bool wmbus_water_update(
    WmbusWaterHistory* h, uint32_t litres, uint32_t now_ms,
    uint32_t window_minutes, uint32_t minimum_litres) {
    if(!h) return false;
    if(window_minutes < 1 || window_minutes > 120) window_minutes = 120;
    if(minimum_litres < 1) minimum_litres = 5;
    uint32_t window = window_minutes * 60000UL;

    /* Single-counter-step quantisation can put >1 L into one telegram.
     * Allow no more than one-quarter of the required window between
     * observed positive increments, with a 2-minute allowance. */
    uint32_t allowed_gap = window / 4 + 120000UL;
    if(allowed_gap > 1920000UL) allowed_gap = 1920000UL;

    if(!h->valid) {
        h->valid = true;
        h->last_litres = litres;
        add_sample(h, litres, now_ms);
        return false;
    }

    uint32_t prev_tick =
        h->ticks[(h->next + WMBUS_WATER_POINTS - 1) % WMBUS_WATER_POINTS];
    uint32_t previous = h->last_litres;
    bool reset = litres < previous;
    uint32_t increment = reset ? 0 : litres - previous;
    uint32_t elapsed_rx = (uint32_t)(now_ms - prev_tick);

    /* Lost radio contact, meter reset, bad reading, or enormous jump:
     * never bridge those discontinuities into a leak event. */
    bool restart = reset || increment > 10000UL || elapsed_rx > 120000UL;
    if(restart) {
        wmbus_water_reset_leak(h);
        if(reset || increment > 10000UL) {
            h->last_flow_tick = 0;
            h->count = h->next = 0;
            h->stable_lph = 0;
            h->rate_primed = false;
        }
    }

    if(!restart) {
        if(h->burst_start_tick == 0 ||
           (uint32_t)(now_ms - h->burst_start_tick) > 300000UL) {
            h->burst_start_tick = now_ms;
            h->burst_litres = 0;
        }
        if(increment > 0) {
            h->last_flow_tick = now_ms;
            h->burst_litres += increment;
        }

        /* A busy 5-minute period is more typical of normal household use
         * than a tiny sustained leak. Hard-reset this leak candidate. */
        bool burst = h->burst_litres >= 5UL;
        bool gap = h->chain_events > 0 &&
                   (uint32_t)(now_ms - h->last_increase_tick) > allowed_gap;
        if(burst || gap) {
            wmbus_water_reset_leak(h);
            if(burst) {
                h->burst_start_tick = now_ms;
                h->burst_litres = 0;
            }
        }

        if(!burst && increment > 0) {
            if(h->chain_events == 0) {
                h->chain_start_tick = now_ms;
            }
            uint32_t elapsed = (uint32_t)(now_ms - h->chain_start_tick);
            /* Prevent very old unrelated activity from being stitched
             * into a leak after a long gap. */
            if(elapsed > window + allowed_gap && !h->leak_active) {
                wmbus_water_reset_leak(h);
                h->chain_start_tick = now_ms;
                elapsed = 0;
            }
            h->last_increase_tick = now_ms;
            if(h->chain_events < UINT8_MAX) h->chain_events++;
            if(UINT32_MAX - h->chain_litres >= increment)
                h->chain_litres += increment;

            uint32_t quarter = window / 4;
            if(quarter == 0) quarter = 1;
            uint32_t which = elapsed / quarter;
            if(which < 4) h->leak_quarters |= (uint8_t)(1u << which);

            /* Reject a candidate whose cumulative use is too high even
             * if packets were quantised or just missed the burst block.
             * The extra 3 L buffers counter quantisation in short time
             * intervals. No floating point is used on Flipper. */
            uint32_t max_litres = 3UL + (uint32_t)(
                ((uint64_t)elapsed * 12UL) / 3600000ULL);
            if(h->chain_litres > max_litres) {
                wmbus_water_reset_leak(h);
            } else if(!h->leak_active &&
                      elapsed >= window &&
                      h->chain_events >= 6 &&
                      h->leak_quarters == 0x0F &&
                      h->chain_litres >= minimum_litres &&
                      ((uint64_t)h->chain_litres * 3600000ULL) <=
                          ((uint64_t)elapsed * 12ULL)) {
                h->leak_active = true;
                h->last_litres = litres;
                add_sample(h, litres, now_ms);
                return true;
            }
        } else if(h->chain_events > 0 && h->last_increase_tick != 0 &&
                  (uint32_t)(now_ms - h->last_increase_tick) > allowed_gap) {
            wmbus_water_reset_leak(h);
        }
    }

    h->last_litres = litres;
    add_sample(h, litres, now_ms);
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
    return h && h->valid && h->last_flow_tick &&
           (uint32_t)(now_ms - h->last_flow_tick) < 20000UL;
}
