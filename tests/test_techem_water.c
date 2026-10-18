/* Regression cases from public wmbusmeters mkradio3/mkradio4 XMQ tests.
 * Protects manufacturer/version/type matching and decoded water volumes. */
#include "../drivers/engine/driver.h"
#include "../protocol/wmbus_link.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const WmbusDriver wmbus_drv_techem_mkradio3;
extern const WmbusDriver wmbus_drv_techem_mkradio4;
extern const WmbusDriver wmbus_drv_techem_mkradio4a;
extern const WmbusDriver wmbus_drv_techem_mkradio3a;
extern const WmbusDriver wmbus_drv_diehl_izar;

const WmbusDriver* const wmbus_engine_registry[] = {
    &wmbus_drv_techem_mkradio3,
    &wmbus_drv_techem_mkradio4,
    &wmbus_drv_techem_mkradio4a,
    &wmbus_drv_techem_mkradio3a,
    &wmbus_drv_diehl_izar,
};
const size_t wmbus_engine_registry_len =
    sizeof(wmbus_engine_registry) / sizeof(wmbus_engine_registry[0]);

static unsigned digit(char c) {
    if(c >= '0' && c <= '9') return (unsigned)(c - '0');
    if(c >= 'a' && c <= 'f') return (unsigned)(c - 'a') + 10;
    if(c >= 'A' && c <= 'F') return (unsigned)(c - 'A') + 10;
    fprintf(stderr, "bad hex digit: %c\n", c);
    exit(1);
}
static size_t unhex(const char* hex, uint8_t* out, size_t cap) {
    size_t n = 0;
    while(*hex) {
        if(*hex == ' ' || *hex == '_') { hex++; continue; }
        if(!hex[1] || n >= cap) exit(1);
        out[n++] = (uint8_t)((digit(hex[0]) << 4) | digit(hex[1]));
        hex += 2;
    }
    return n;
}

static void run(const char* name, const char* hex,
                const WmbusDriver* expected, const char* total) {
    uint8_t raw[300];
    size_t len = unhex(hex, raw, sizeof(raw));
    WmbusLinkFrame link;
    assert(wmbus_link_parse(raw, len, &link));
    WmbusDecodeCtx ctx = {
        .manuf = link.M, .version = link.version, .medium = link.medium,
        .ci = link.ci, .id = link.id, .apdu = link.apdu, .apdu_len = link.apdu_len,
    };
    const WmbusDriver* driver = wmbus_engine_find_ctx(&ctx);
    if(driver != expected) {
        fprintf(stderr, "%s: wrong driver '%s', expected '%s'\n",
                name, driver->id, expected->id);
        exit(1);
    }
    char out[512] = {0};
    wmbus_engine_decode_ctx(&ctx, out, sizeof(out));
    if(!strstr(out, total)) {
        fprintf(stderr, "%s: wrong decoded water volume, got:\n%s\n", name, out);
        exit(1);
    }
    printf("%s: %s\n", name, total);
}

static void expect(const char* name, uint8_t version, uint8_t medium,
                   const WmbusDriver* driver) {
    WmbusDecodeCtx ctx = {
        .manuf = 0x5068, .version = version, .medium = medium,
        .ci = 0xA2,
    };
    if(wmbus_engine_find_ctx(&ctx) != driver) {
        fprintf(stderr, "%s: incorrect MVT match %02X/%02X\n",
                name, version, medium);
        exit(1);
    }
}

int main(void) {
    /* Reference: wmbusmeters drivers/src/mkradio3.xmq */
    run("Techem MK-Radio3 (warm)",
        "2F446850313233347462A2_069F255900B029310000000306060906030609070606050509050505050407040605070500",
        &wmbus_drv_techem_mkradio3, "13.8 m3");
    /* Reference: wmbusmeters drivers/src/mkradio4.xmq */
    run("Techem MK-Radio4 (warm)",
        "2F446850200141029562A2_06702901006017030004000300000000000000000000000000000000000000000000000000",
        &wmbus_drv_techem_mkradio4, "0.4 m3");

    expect("MK-Radio3 cold", 0x74, 0x72, &wmbus_drv_techem_mkradio3);
    expect("MK-Radio4 warm v70", 0x70, 0x62, &wmbus_drv_techem_mkradio4);
    expect("MK-Radio4 cold v70", 0x70, 0x72, &wmbus_drv_techem_mkradio4);
    expect("MK-Radio4 cold v95", 0x95, 0x72, &wmbus_drv_techem_mkradio4);
    expect("MK-Radio3a cold", 0x50, 0x72, &wmbus_drv_techem_mkradio3a);
    expect("MK-Radio4a", 0x95, 0x37, &wmbus_drv_techem_mkradio4a);

    assert(wmbus_engine_find(0x5068, 0x62, 0x74)
           != &wmbus_drv_techem_mkradio3);
    puts("All Techem water tests passed.");
    return 0;
}
