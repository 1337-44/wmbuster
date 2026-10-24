#include "wmbus_radio.h"
/* Selects the active CC1101 on the Flipper or an external SPI plugin.
 * "External" is strict: a missing/disconnected module must never silently
 * receive with the built-in transceiver. */

#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_power.h>

#define TAG "WmbusRadio"

/* Share GPIO 5 V with the GPS receiver or a user-enabled power rail.
 * Only disable it if THIS radio code turned it on. */
static bool s_otg_owned = false;

static bool otg_power_on(void) {
    if(furi_hal_power_is_otg_enabled()) return true;
    for(uint8_t tries = 0; tries < 5; ++tries) {
        if(furi_hal_power_enable_otg()) {
            s_otg_owned = true;
            furi_delay_ms(25);
            return true;
        }
        furi_delay_ms(25);
    }
    FURI_LOG_E(TAG, "GPIO 5V could not be enabled");
    return false;
}

static void otg_power_off(void) {
    if(s_otg_owned) {
        if(furi_hal_power_is_otg_enabled()) furi_hal_power_disable_otg();
        s_otg_owned = false;
    }
}

static const SubGhzDevice* external_plugin(void) {
    const SubGhzDevice* d = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
    if(!d) {
        FURI_LOG_E(TAG, "External CC1101 radio plugin missing");
    }
    return d;
}

bool wmbus_radio_detect_external(void) {
    const SubGhzDevice* d = external_plugin();
    if(!d) return false;
    bool was_powered = furi_hal_power_is_otg_enabled();
    if(!otg_power_on()) return false;
    bool connected = subghz_devices_is_connect(d);
    if(!was_powered) otg_power_off();
    return connected;
}

const SubGhzDevice* wmbus_radio_select(
    const SubGhzDevice* current,
    WmbusModuleSetting module) {

    if(module == WmbusModuleAuto || module == WmbusModuleExternal) {
        const SubGhzDevice* ext = external_plugin();
        if(ext && otg_power_on()) {
            /* Keep 5 V enabled throughout the probe AND begin() call.
             * Older code cycled power between these stages and ignored
             * begin() returning false. */
            if(subghz_devices_is_connect(ext)) {
                if(current == ext) return ext;

                if(subghz_devices_begin(ext)) {
                    if(current) wmbus_radio_release(current);
                    FURI_LOG_I(TAG, "Using external CC1101");
                    return ext;
                }
                /* cc1101_ext begin() allocates a SPI handle even if chip
                 * initialisation fails; end() frees this partial state. */
                subghz_devices_end(ext);
                FURI_LOG_E(TAG, "External CC1101 init failed");
            } else {
                FURI_LOG_E(TAG, "External CC1101 not responding over SPI");
            }
            otg_power_off();
        }
        if(module == WmbusModuleExternal) {
            /* Crucial: never fall back to the internal CC1101. */
            FURI_LOG_E(TAG, "External selected: radio unavailable, no fallback");
            return NULL;
        }
        FURI_LOG_W(TAG, "Auto: external unavailable; using internal CC1101");
    }

    const SubGhzDevice* internal =
        subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(!internal) {
        FURI_LOG_E(TAG, "Internal CC1101 device not registered");
        return NULL;
    }
    if(current == internal) return internal;
    if(current) wmbus_radio_release(current);
    /* Internal interconnect has .begin = NULL in official firmware.
     * Therefore subghz_devices_begin(internal) returns false even
     * though its HAL is perfectly usable. Do not treat that as failure. */
    subghz_devices_begin(internal);
    FURI_LOG_I(TAG, "Using internal CC1101");
    return internal;
}

bool wmbus_radio_is_external(const SubGhzDevice* dev) {
    if(!dev) return false;
    const SubGhzDevice* internal =
        subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    return dev != internal;
}

void wmbus_radio_release(const SubGhzDevice* dev) {
    if(!dev) return;
    const SubGhzDevice* internal =
        subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(dev != internal) subghz_devices_end(dev);
    otg_power_off();
}
