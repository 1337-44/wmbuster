/* Techem MK-Radio 3/4 water meters and MK-Radio4a OMS variant.
 * MK-Radio3 carries dates, MK-Radio4 uses compact private data, whereas
 * MK-Radio4a has OMS content and must not share MK-Radio4 offsets.
 * References: wmbusmeters drivers/src/{mkradio3,mkradio4,mkradio4a}.xmq. */

#include "../../engine/driver.h"
#include "_compact.h"

/* MK-Radio 3: version 0x74, medium 0x62 (warm) or 0x72 (cold).
 * Reference: wmbusmeters drivers/src/mkradio3.xmq. */
static const WmbusMVT k_mvt_mk3[] = {
    {"TCH", 0x74, 0x62},
    {"TCH", 0x74, 0x72},
    { {0}, 0, 0 }
};
static size_t decode_mk3(uint16_t m, uint8_t v, uint8_t med,
                         const uint8_t* a, size_t l, char* o, size_t cap) {
    (void)m; (void)v; (void)med;
    return techem_decode_water(/* with_dates */ true, a, l, o, cap);
}
const WmbusDriver wmbus_drv_techem_mkradio3 = {
    .id = "techem-mkradio3",
    .title = "Techem MK-Radio3 water",
    .mvt = k_mvt_mk3,
    .decode = decode_mk3,
};

/* MK-Radio 4: version 0x70/0x95, medium 0x62 (warm) / 0x72 (cold).
 * The 4a variant is routed separately using the OMS parser.
 * Reference: wmbusmeters drivers/src/mkradio4.xmq. */
static const WmbusMVT k_mvt_mk4[] = {
    {"TCH", 0x70, 0x62},
    {"TCH", 0x70, 0x72},
    {"TCH", 0x95, 0x62},
    {"TCH", 0x95, 0x72},
    /* MK-Radio4a frames use a separate OMS layout, below. */
    { {0}, 0, 0 }
};
static size_t decode_mk4(uint16_t m, uint8_t v, uint8_t med,
                         const uint8_t* a, size_t l, char* o, size_t cap) {
    (void)m; (void)v; (void)med;
    return techem_decode_water(/* with_dates */ false, a, l, o, cap);
}
const WmbusDriver wmbus_drv_techem_mkradio4 = {
    .id = "techem-mkradio4",
    .title = "Techem MK-Radio4 water",
    .mvt = k_mvt_mk4,
    .decode = decode_mk4,
};

/* Do not decode MK-Radio4a using the proprietary MK-Radio4 fixed offsets.
 * Reference: wmbusmeters drivers/src/mkradio4a.xmq. Its extended OMS
 * payload (often encrypted) must be handled by the normal OMS pipeline. */
static const WmbusMVT k_mvt_mk4a[] = {
    {"TCH", 0x95, 0x37},
    {"HYD", 0xFE, 0x06},
    { {0}, 0, 0 }
};
const WmbusDriver wmbus_drv_techem_mkradio4a = {
    .id = "techem-mkradio4a",
    .title = "Techem MK-Radio4a water",
    .mvt = k_mvt_mk4a,
    .decode = NULL,
};
