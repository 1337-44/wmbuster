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

static void test_leak(void) {
    WmbusWaterHistory h = {0};
    uint32_t start = 10000;
    /* One jump/isolated draw must not alert. */
    assert(!wmbus_water_update(&h, 1000, start, 30, 1));
    assert(!wmbus_water_update(&h, 1001, start + 60000, 30, 1));
    assert(!wmbus_water_update(&h, 1001, start + 200000, 30, 1));
    /* 20-minute pause breaks continuous-flow detection. */
    assert(!wmbus_water_update(&h, 1002, start + 1260000, 30, 1));
    assert(h.chain_events == 1);
    /* Continuous flow in 10-min steps over 30 minutes. */
    assert(!wmbus_water_update(&h, 1003, start + 1860000, 30, 1));
    assert(!wmbus_water_update(&h, 1004, start + 2460000, 30, 1));
    assert(wmbus_water_update(&h, 1005, start + 3060000, 30, 1));
    assert(h.leak_active);
    assert(!wmbus_water_update(&h, 1005, start + 3068000, 30, 1));
    /* Meter reset must clear the alarm and the trend. */
    assert(!wmbus_water_update(&h, 10, start + 3076000, 30, 1));
    assert(!h.leak_active);
    assert(h.count == 1);
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
    test_short_events_and_rate();
    puts("Water history / leak detector tests passed.");
    return 0;
}
