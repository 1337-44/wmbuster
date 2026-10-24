#pragma once
/* Thin wrapper around `subghz_devices_*` that chooses a CC1101 and
 * manages the shared GPIO 5V rail. Auto alone may fall back internally.
 * Explicit External returns NULL when the external radio is unavailable. */

#include <lib/subghz/devices/devices.h>
#include "../wmbus_app_i.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Probe GPIO SPI for an external CC1101 (same detection as the
 * Flux Capacitor / Sub-GHz app: OTG power + subghz_devices_is_connect).
 * Returns true if a working external CC1101 answers. */
bool wmbus_radio_detect_external(void);

/* Select active CC1101:
 *   Auto     – probes external, falls back to internal if unavailable.
 *   Internal – only internal.
 *   External – only external, NULL on detection/initialization failure. */
const SubGhzDevice* wmbus_radio_select(
    const SubGhzDevice* current,
    WmbusModuleSetting module);

bool wmbus_radio_is_external(const SubGhzDevice* dev);

void wmbus_radio_release(const SubGhzDevice* dev);

#ifdef __cplusplus
}
#endif
