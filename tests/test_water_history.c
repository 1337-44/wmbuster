/* Host-side water meter regression tests, no Flipper SDK required. */
#include "../water_history.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_parser(void) {
    uint32_t litres = 0;
    assert(wmbus_water_parse("Techem water\nNow 16.7 m3\nPeriod 0.3 m3\n", &litres));
    assert(litres == 16700);
    assert(wmbus_water_parse("Diehl IZAR water\nTotal 16.760 m3\n", &litres));
    assert(litres == 16760);
    assert(wmbus_water_parse("Now 0 m3\n", &litres) && litres == 0);
    assert(!wmbus_water_parse("Techem heat\nTotal 100 kWh\n", &litres));
    assert(!wmbus_water_parse("Now 123.1234 m3\n", &litres));
    assert(!wmbus_water_parse("Now -1.3 m3\n", &litres));
    assert(!wmbus_water_parse("Now 9999999999999 m3\n", &litres));
}

/* Conservative leak detector verification using real 8-second telegram
 * cadence and 1 L cumulative-counter quantisation (as in IZAR). */
static void test_leak(void) {
    WmbusWaterHistory h = {0};
    uint32_t start = 10000;
    assert(!wmbus_water_update(&h, 1000, start, 120, 5));
    /* A tap drawing several litres in a few minutes is NOT a leak. */
    for(uint32_t i = 1; i <= 60; i++) {
        uint32_t lit = 1000 + (i * 8 * 130) / 3600; /* 130 L/h normal use */
        assert(!wmbus_water_update(&h, lit, start + i * 8000, 120, 5));
    }
    assert(!h.leak_active);
    /* Meter reset clears the alarm/plot without manufacturing a delta. */
    assert(!wmbus_water_update(&h, 10, start + 61 * 8000, 120, 5));
    assert(!h.leak_active);
    assert(h.count == 1);
}

static void test_normal_household_use(void) {
    WmbusWaterHistory h = {0};
    const uint32_t start = 10000;
    uint32_t litres = 1000;
    /* Two and a half hours of occasional 5 L draws, every 20 min:
     * not continuous low flow, regardless of total consumption. */
    for(uint32_t i = 0; i < 1200; i++) {
        if(i && i % 150 == 0) litres += 5;
        assert(!wmbus_water_update(&h, litres, start + i * 8000, 120, 5));
    }
    assert(!h.leak_active);
}

static void test_sustained_small_leak(void) {
    WmbusWaterHistory h = {0};
    uint32_t start = 10000;
    unsigned alerts = 0;
    /* 3 L/h: 1 L roughly every 20 minutes, for 3.5 hours. */
    for(uint32_t i = 0; i < 1575; i++) {
        uint32_t litres = 1000 + (i * 8 * 3) / 3600;
        if(wmbus_water_update(&h, litres, start + i * 8000, 120, 5)) alerts++;
    }
    assert(alerts == 1);
    assert(h.leak_active);
}

static void test_fast_constant_consumption(void) {
    WmbusWaterHistory h = {0};
    uint32_t start = 10000;
    /* Ordinary steady flow 130 L/h over 2.5 hours should NOT alarm. */
    for(uint32_t i = 0; i < 1125; i++) {
        uint32_t litres = 1000 + (i * 8 * 130) / 3600;
        assert(!wmbus_water_update(&h, litres, start + i * 8000, 120, 5));
    }
    assert(!h.leak_active);
}

static void test_short_old_threshold(void) {
    WmbusWaterHistory h = {0};
    uint32_t start = 10000;
    /* The old 15 min/1 L configuration used to classify everyday activity
     * as leakage. Even when explicitly retained, the new safeguards must
     * NOT accept 24 L/h sustained consumption as a tiny leak. */
    for(uint32_t i = 0; i < 225; i++) {
        uint32_t litres = 1000 + (i * 8 * 24) / 3600;
        assert(!wmbus_water_update(&h, litres, start + i * 8000, 15, 1));
    }
}

static void test_mean_telegram_interval(void) {
    WmbusWaterHistory h = {0};
    assert(wmbus_water_avg_interval_ms(&h) == 0);
    assert(!wmbus_water_update(&h, 1000, 10000, 120, 5));
    assert(wmbus_water_avg_interval_ms(&h) == 0);
    for(uint32_t i = 1; i < 26; ++i) {
        /* 8-second reception cadence and zero water consumption. */
        assert(!wmbus_water_update(&h, 1000, 10000 + i * 8000, 120, 5));
        assert(wmbus_water_avg_interval_ms(&h) == 8000);
    }
    assert(h.count == WMBUS_WATER_POINTS); /* ring wraps without changing mean */
    /* Delayed packet increases the mean; 1.5 * avg separates missing
     * telegram state from perfectly normal zero-consumption readings. */
    assert(!wmbus_water_update(&h, 1000, 10000 + 26 * 8000 + 4000, 120, 5));
    assert(wmbus_water_avg_interval_ms(&h) > 8000);

    WmbusWaterHistory wrap = {0};
    assert(!wmbus_water_update(&wrap, 5, UINT32_MAX - 3999, 120, 5));
    assert(!wmbus_water_update(&wrap, 5, 4000, 120, 5));
    assert(wmbus_water_avg_interval_ms(&wrap) == 8000);
}

static void test_recent_flow_survives_leak_reset(void) {
    WmbusWaterHistory h = {0};
    assert(!wmbus_water_update(&h, 2000, 10000, 120, 5));
    assert(!wmbus_water_update(&h, 2001, 18000, 120, 5));
    assert(wmbus_water_recent_flow(&h, 19000));
    /* With Leak detect Off, code explicitly resets the alarm candidate.
     * That must NOT disable the blinking flow indicator in BIG. */
    wmbus_water_reset_leak(&h);
    assert(wmbus_water_recent_flow(&h, 19000));
    assert(!h.leak_active);
}

static void test_radio_outage_breaks_sequence(void) {
    WmbusWaterHistory h = {0};
    uint32_t start = 10000;
    for(uint32_t i = 0; i < 450; i++) {
        uint32_t litres = 1000 + (i * 8 * 3) / 3600;
        assert(!wmbus_water_update(&h, litres, start + i * 8000, 120, 5));
    }
    /* Reception gap >2 min breaks continuity, not counted as "no flow". */
    assert(!wmbus_water_update(&h, 1004, start + 450 * 8000 + 300000, 120, 5));
    assert(!h.leak_active);
    assert(h.chain_events == 0);
}


static void test_short_events_and_rate(void) {
    WmbusWaterHistory h = {0};
    assert(!wmbus_water_update(&h, 0, 1000, 15, 5));
    assert(!wmbus_water_update(&h, 10, 11000, 15, 5));
    assert(wmbus_water_rate_lph(&h) == 3600);
    assert(wmbus_water_recent_flow(&h, 11000));
    assert(!wmbus_water_recent_flow(&h, 32000));
    for(int i = 0; i < 50; i++) {
        assert(!wmbus_water_update(&h, 10, 19000 + (uint32_t)i*8000, 15, 5));
    }
    assert(h.count == WMBUS_WATER_POINTS);
    assert(!wmbus_water_update(&h, 10050, 430000, 15, 5));
    assert(!h.leak_active);
}

int main(void) {
    test_parser();
    test_leak();
    test_normal_household_use();
    test_sustained_small_leak();
    test_fast_constant_consumption();
    test_short_old_threshold();
    test_recent_flow_survives_leak_reset();
    test_mean_telegram_interval();
    test_radio_outage_breaks_sequence();
    test_short_events_and_rate();
    puts("Water history / leak detector tests passed.");
    return 0;
}
