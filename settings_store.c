/* Versioned settings persistence, independent of telegram logs. */
#include "settings_store.h"
#include "wmbus_app_i.h"
#include <stdio.h>
#include <string.h>

static void parse_setting(WmbusSettings* s, const char* line) {
    unsigned v;
    if(sscanf(line, "mode=%u", &v) == 1) {
        if(v < WmbusMode_Count) s->mode = (WmbusMode)v;
    } else if(sscanf(line, "filter=%u", &v) == 1) {
        if(v < WmbusFilter_Count) s->filter = (WmbusFilter)v;
    } else if(sscanf(line, "sort=%u", &v) == 1) {
        if(v < WmbusSort_Count) s->sort = (WmbusSort)v;
    } else if(sscanf(line, "module=%u", &v) == 1) {
        if(v < WmbusModule_Count) s->module = (WmbusModuleSetting)v;
    } else if(sscanf(line, "logging=%u", &v) == 1) {
        if(v < 2) s->logging = (v != 0);
    } else if(sscanf(line, "sound=%u", &v) == 1) {
        if(v < 2) s->sound_enabled = (v != 0);
    } else if(sscanf(line, "vibration=%u", &v) == 1) {
        if(v < 2) s->vibration_enabled = (v != 0);
    } else if(sscanf(line, "led=%u", &v) == 1) {
        if(v < 2) s->led_enabled = (v != 0);
    } else if(sscanf(line, "gps=%u", &v) == 1) {
        if(v < 2) s->gps_enabled = (v != 0);
    } else if(sscanf(line, "leak=%u", &v) == 1) {
        if(v < 2) s->leak_enabled = v != 0;
    } else if(sscanf(line, "leak_scope=%u", &v) == 1) {
        if(v < 2) s->leak_scope = (uint8_t)v;
    } else if(sscanf(line, "leak_window=%u", &v) == 1) {
        if(v < 4) s->leak_window_index = (uint8_t)v;
    } else if(sscanf(line, "leak_min=%u", &v) == 1) {
        if(v < 4) s->leak_min_index = (uint8_t)v;
    }
}

void wmbus_settings_load(WmbusApp* app) {
    if(!app || !app->storage) return;
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, WMBUS_APP_SETTINGS_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[256];
        uint16_t n = storage_file_read(f, buf, sizeof(buf)-1);
        buf[n] = 0;
        bool from_v1 = n >= 10 && strncmp(buf, "version=1\n", 10) == 0;
        bool from_v2 = n >= 10 && strncmp(buf, "version=2\n", 10) == 0;
        if(from_v1 || from_v2) {
            /* Firmware FAP API does not export strtok().
             * Parse CR/LF-delimited lines in-place without libc tokenizer
             * dependencies or shared strtok state. */
            char* p = buf;
            char* end = buf + n;
            while(p < end && *p) {
                char* start = p;
                while(p < end && *p && *p != '\r' && *p != '\n') ++p;
                char sep = p < end ? *p : '\0';
                if(p < end) *p++ = '\0';
                if(start != p - (sep != '\0' ? 1 : 0)) {
                    parse_setting(&app->settings, start);
                }
                (void)sep;
                while(p < end && (*p == '\r' || *p == '\n')) ++p;
            }
            /* Old experimental builds used 15 min / 1 L as a very
             * permissive leak threshold, causing normal-use alerts.
             * Migrate only this risky old combination once. All other
             * user preferences remain unchanged. New saves use v2. */
            if(from_v1 && app->settings.leak_window_index == 0 &&
               app->settings.leak_min_index == 0) {
                app->settings.leak_window_index = 3; /* 120 min */
                app->settings.leak_min_index = 1; /* 5 L */
            }
        }
        storage_file_close(f);
    }
    storage_file_free(f);
    app->settings.freq_hz = app->settings.mode == WmbusModeS1 ? 868300000 : 868950000;
}

bool wmbus_settings_save(WmbusApp* app) {
    if(!app || !app->storage) return false;
    storage_simply_mkdir(app->storage, WMBUS_APP_DATA_DIR);
    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "version=2\nmode=%u\nfilter=%u\nsort=%u\nmodule=%u\n"
        "logging=%u\nsound=%u\nvibration=%u\nled=%u\ngps=%u\n"
        "leak=%u\nleak_scope=%u\nleak_window=%u\nleak_min=%u\n",
        (unsigned)app->settings.mode,
        (unsigned)app->settings.filter,
        (unsigned)app->settings.sort,
        (unsigned)app->settings.module,
        (unsigned)app->settings.logging,
        (unsigned)app->settings.sound_enabled,
        (unsigned)app->settings.vibration_enabled,
        (unsigned)app->settings.led_enabled,
        (unsigned)app->settings.gps_enabled,
        (unsigned)app->settings.leak_enabled,
        (unsigned)app->settings.leak_scope,
        (unsigned)app->settings.leak_window_index,
        (unsigned)app->settings.leak_min_index);
    if(len <= 0 || len >= (int)sizeof(buf)) return false;
    File* f = storage_file_alloc(app->storage);
    bool ok = false;
    if(storage_file_open(f, WMBUS_APP_SETTINGS_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = storage_file_write(f, buf, (size_t)len) == (size_t)len;
        storage_file_close(f);
    }
    storage_file_free(f);
    return ok;
}
