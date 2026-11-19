/* Run:
 * gcc -std=c11 -Wall -Wextra -Werror -I. tests/test_cul_proto.c \
 *     subghz/wmbus_cul_proto.c protocol/wmbus_crc.c -o /tmp/test_cul
 * /tmp/test_cul
 * Public telemetry samples reported in wmbusmeters debug logs. */
#include "../subghz/wmbus_cul_proto.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char* k_format_a =
    "b2944A511780735321315931CA1111100135D114581B59070615F689AD5"
    "5EF517FDA5101C5464C0FD0B93A08FF474D8C18315";
static const char* k_format_b =
    "bY3444372C964001233A168D20B96060C42109CFA2C3EE1DF1B12BE6D"
    "E28F6986B9AA8625209FBD47B10CC7B1481EDD00D5D1B25E15C8625";

int main(void) {
    uint8_t data[290];
    size_t n;
    int8_t rssi;
    assert(wmbus_cul_parse_line("CMODE", data, sizeof(data), &n, &rssi) ==
           WmbusCulParseIgnore);
    assert(wmbus_cul_parse_line(k_format_a, data, sizeof(data), &n, &rssi) ==
           WmbusCulParseFrameA);
    assert(n == 42);
    assert(data[0] == 0x29 && data[1] == 0x44);
    assert(rssi == -64);

    assert(wmbus_cul_parse_line(k_format_b, data, sizeof(data), &n, &rssi) ==
           WmbusCulParseFrameB);
    assert(n == 51);
    assert(data[0] == 0x32 && data[1] == 0x44); /* B CRC removal adjusts L */
    assert(rssi == -56);

    char no_suffix[160];
    strcpy(no_suffix, k_format_a);
    no_suffix[strlen(no_suffix) - 4] = 0; /* remove LQI+RSSI */
    assert(wmbus_cul_parse_line(no_suffix, data, sizeof(data), &n, &rssi) ==
           WmbusCulParseFrameA);
    assert(rssi == -127);

    char invalid[160];
    strcpy(invalid, k_format_a);
    invalid[14] = invalid[14] == 'A' ? 'B' : 'A';
    assert(wmbus_cul_parse_line(invalid, data, sizeof(data), &n, &rssi) ==
           WmbusCulParseBad);

    invalid[strlen(invalid) - 1] = 0; /* odd hex string */
    assert(wmbus_cul_parse_line(invalid, data, sizeof(data), &n, &rssi) ==
           WmbusCulParseBad);

    assert(wmbus_cul_parse_line("brt", data, sizeof(data), &n, &rssi) ==
           WmbusCulParseIgnore);

    puts("nanoCUL format A/B parser: OK");
    return 0;
}
