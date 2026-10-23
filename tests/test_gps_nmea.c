/* Host regression test of vendored minmea GPS parser.
 * NMEA examples from minmea upstream with verified checksums. */
#include "../gps/minmea.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const char* rmc =
        "$GPRMC,081836,A,3751.65,S,14507.36,E,000.0,360.0,130998,011.3,E*62";
    const char* gga =
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    const char* corrupt =
        "$GPRMC,081836,A,3751.65,S,14507.36,E,000.0,360.0,130998,011.3,E*00";
    assert(minmea_sentence_id(rmc, true) == MINMEA_SENTENCE_RMC);
    struct minmea_sentence_rmc r;
    assert(minmea_parse_rmc(&r, rmc));
    assert(r.valid);
    assert(r.date.year == 98 && r.date.month == 9 && r.date.day == 13);
    assert(r.time.hours == 8 && r.time.minutes == 18 && r.time.seconds == 36);
    float lat = minmea_tocoord(&r.latitude);
    float lon = minmea_tocoord(&r.longitude);
    assert(lat < -37.86f && lat > -37.87f);
    assert(lon > 145.12f && lon < 145.13f);
    assert(minmea_sentence_id(gga, true) == MINMEA_SENTENCE_GGA);
    struct minmea_sentence_gga g;
    assert(minmea_parse_gga(&g, gga));
    assert(g.fix_quality == 1 && g.satellites_tracked == 8);
    assert(minmea_sentence_id(corrupt, true) == MINMEA_INVALID);
    puts("NMEA RMC/GGA GPS checksum and field tests passed.");
    return 0;
}
