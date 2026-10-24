/* Settings view. Frequency is implied by mode: T1/C1/CT use 868.95 MHz,
 * S1 uses 868.30 MHz — there is no separate frequency picker. */

#include "../wmbus_app_i.h"
#include "../settings_store.h"
#include "../gps/gps_receiver.h"
#include <stdio.h>

static const char* k_mode_names[]   = { "T1", "C1", "T+C", "S1" };
static const char* k_filter_names[] = { "All", "Top 10", "Top 5", "Top 3" };
static const char* k_sort_names[]   = { "Signal", "Recent", "ID", "Packets" };
static const char* k_module_names[] = { "Auto", "Internal", "External" };
static const char* k_toggle[] = { "Off", "On" };
static const char* k_leak_windows[] = { "15 min", "30 min", "60 min", "120 min" };
static const char* k_leak_mins[] = { "1 L", "5 L", "10 L", "50 L" };

/* Keep the sparkline/history; restart only leak continuity after a policy change. */
static void reset_leak_state(WmbusApp* app) {
    if(furi_mutex_acquire(app->lock, 50) != FuriStatusOk) return;
    for(size_t i = 0; i < app->meter_count; ++i) {
        WmbusWaterHistory* h = &app->meters[i].water;
        h->chain_events = 0;
        h->chain_litres = 0;
        h->chain_start_tick = 0;
        h->last_increase_tick = 0;
        h->leak_active = false;
    }
    furi_mutex_release(app->lock);
}

static uint32_t freq_for_mode(WmbusMode m) {
    return (m == WmbusModeS1) ? 868300000 : 868950000;
}

static void on_mode_change(VariableItem* it) {
    WmbusApp* app = variable_item_get_context(it);
    uint8_t i = variable_item_get_current_value_index(it);
    if(i >= WmbusMode_Count) i = 0;
    app->settings.mode    = (WmbusMode)i;
    app->settings.freq_hz = freq_for_mode((WmbusMode)i);
    variable_item_set_current_value_text(it, k_mode_names[i]);
    wmbus_settings_save(app);
}

static void on_filter_change(VariableItem* it) {
    WmbusApp* app = variable_item_get_context(it);
    uint8_t i = variable_item_get_current_value_index(it);
    if(i >= WmbusFilter_Count) i = 0;
    app->settings.filter = (WmbusFilter)i;
    variable_item_set_current_value_text(it, k_filter_names[i]);
    wmbus_settings_save(app);
}

static void on_sort_change(VariableItem* it) {
    WmbusApp* app = variable_item_get_context(it);
    uint8_t i = variable_item_get_current_value_index(it);
    if(i >= WmbusSort_Count) i = 0;
    app->settings.sort = (WmbusSort)i;
    variable_item_set_current_value_text(it, k_sort_names[i]);
    wmbus_settings_save(app);
}

static void on_log_change(VariableItem* it) {
    WmbusApp* app = variable_item_get_context(it);
    app->settings.logging = variable_item_get_current_value_index(it) ? true : false;
    variable_item_set_current_value_text(it, app->settings.logging ? "On" : "Off");
    wmbus_settings_save(app);
}

static void on_module_change(VariableItem* it) {
    WmbusApp* app = variable_item_get_context(it);
    uint8_t i = variable_item_get_current_value_index(it);
    if(i >= WmbusModule_Count) i = 0;
    app->settings.module = (WmbusModuleSetting)i;
    variable_item_set_current_value_text(it, k_module_names[i]);
    wmbus_settings_save(app);
}

static void on_sound_change(VariableItem* it) {
    WmbusApp* app = variable_item_get_context(it);
    app->settings.sound_enabled = variable_item_get_current_value_index(it) != 0;
    variable_item_set_current_value_text(it, k_toggle[app->settings.sound_enabled]);
    wmbus_settings_save(app);
}
static void on_vibration_change(VariableItem* it) {
    WmbusApp* app = variable_item_get_context(it);
    app->settings.vibration_enabled = variable_item_get_current_value_index(it) != 0;
    variable_item_set_current_value_text(it, k_toggle[app->settings.vibration_enabled]);
    wmbus_settings_save(app);
}
static void on_led_change(VariableItem* it) {
    WmbusApp* app = variable_item_get_context(it);
    app->settings.led_enabled = variable_item_get_current_value_index(it) != 0;
    variable_item_set_current_value_text(it, k_toggle[app->settings.led_enabled]);
    wmbus_settings_save(app);
}

static void on_gps_change(VariableItem* it) {
    WmbusApp* app = variable_item_get_context(it);
    bool enable = variable_item_get_current_value_index(it) != 0;
    if(enable && !app->settings.gps_enabled) {
        app->gps = wmbus_gps_start();
    } else if(!enable && app->settings.gps_enabled) {
        if(app->gps) wmbus_gps_stop(app->gps);
        app->gps = NULL;
    }
    app->settings.gps_enabled = enable;
    app->gps_last_log_ok = false;
    variable_item_set_current_value_text(it, k_toggle[enable]);
    wmbus_settings_save(app);
}

static void on_leak_enabled_change(VariableItem* it) {
    WmbusApp* app = variable_item_get_context(it);
    bool enabled = variable_item_get_current_value_index(it) != 0;
    if(enabled != app->settings.leak_enabled) reset_leak_state(app);
    app->settings.leak_enabled = enabled;
    variable_item_set_current_value_text(it, k_toggle[enabled]);
    wmbus_settings_save(app);
}
static const char* k_leak_scopes[] = { "All", "Watched" };
static void on_leak_scope_change(VariableItem* it) {
    WmbusApp* app = variable_item_get_context(it);
    uint8_t value = variable_item_get_current_value_index(it);
    if(value > 1) value = 0;
    if(value != app->settings.leak_scope) reset_leak_state(app);
    app->settings.leak_scope = value;
    variable_item_set_current_value_text(it, k_leak_scopes[value]);
    wmbus_settings_save(app);
}
static void on_leak_window_change(VariableItem* it) {
    WmbusApp* app = variable_item_get_context(it);
    uint8_t n = variable_item_get_current_value_index(it);
    if(n >= 4) n = 1;
    if(n != app->settings.leak_window_index) reset_leak_state(app);
    app->settings.leak_window_index = n;
    variable_item_set_current_value_text(it, k_leak_windows[n]);
    wmbus_settings_save(app);
}
static void on_leak_min_change(VariableItem* it) {
    WmbusApp* app = variable_item_get_context(it);
    uint8_t n = variable_item_get_current_value_index(it);
    if(n >= 4) n = 0;
    if(n != app->settings.leak_min_index) reset_leak_state(app);
    app->settings.leak_min_index = n;
    variable_item_set_current_value_text(it, k_leak_mins[n]);
    wmbus_settings_save(app);
}

void wmbus_view_settings_enter(void* ctx) {
    WmbusApp* app = ctx;
    /* Stop the radio so mode changes apply on next start. */
    wmbus_scanning_stop(app);
    variable_item_list_reset(app->var_list);

    VariableItem* it;
    it = variable_item_list_add(app->var_list, "Mode", WmbusMode_Count, on_mode_change, app);
    variable_item_set_current_value_index(it, app->settings.mode);
    variable_item_set_current_value_text(it, k_mode_names[app->settings.mode]);

    /* Top-N filters are useful when 100+ neighbour meters dominate. */
    it = variable_item_list_add(app->var_list, "Filter", WmbusFilter_Count,
                                on_filter_change, app);
    variable_item_set_current_value_index(it, app->settings.filter);
    variable_item_set_current_value_text(it, k_filter_names[app->settings.filter]);

    it = variable_item_list_add(app->var_list, "Sort by", WmbusSort_Count,
                                on_sort_change, app);
    variable_item_set_current_value_index(it, app->settings.sort);
    variable_item_set_current_value_text(it, k_sort_names[app->settings.sort]);

    it = variable_item_list_add(app->var_list, "Log to SD", 2, on_log_change, app);
    variable_item_set_current_value_index(it, app->settings.logging ? 1 : 0);
    variable_item_set_current_value_text(it, app->settings.logging ? "On" : "Off");

    it = variable_item_list_add(app->var_list, "Module", WmbusModule_Count,
                                on_module_change, app);
    variable_item_set_current_value_index(it, app->settings.module);
    variable_item_set_current_value_text(it, k_module_names[app->settings.module]);

    it = variable_item_list_add(app->var_list, "Sound", 2, on_sound_change, app);
    variable_item_set_current_value_index(it, app->settings.sound_enabled ? 1 : 0);
    variable_item_set_current_value_text(it, k_toggle[app->settings.sound_enabled]);

    it = variable_item_list_add(app->var_list, "Vibration", 2, on_vibration_change, app);
    variable_item_set_current_value_index(it, app->settings.vibration_enabled ? 1 : 0);
    variable_item_set_current_value_text(it, k_toggle[app->settings.vibration_enabled]);

    it = variable_item_list_add(app->var_list, "Scan LED", 2, on_led_change, app);
    variable_item_set_current_value_index(it, app->settings.led_enabled ? 1 : 0);
    variable_item_set_current_value_text(it, k_toggle[app->settings.led_enabled]);

    it = variable_item_list_add(app->var_list, "GPS UART", 2, on_gps_change, app);
    variable_item_set_current_value_index(it, app->settings.gps_enabled ? 1 : 0);
    variable_item_set_current_value_text(it, k_toggle[app->settings.gps_enabled]);

    it = variable_item_list_add(app->var_list, "Leak detect", 2, on_leak_enabled_change, app);
    variable_item_set_current_value_index(it, app->settings.leak_enabled ? 1 : 0);
    variable_item_set_current_value_text(it, k_toggle[app->settings.leak_enabled]);

    it = variable_item_list_add(app->var_list, "Leak window", 4, on_leak_window_change, app);
    variable_item_set_current_value_index(it, app->settings.leak_window_index);
    variable_item_set_current_value_text(it, k_leak_windows[app->settings.leak_window_index]);

    it = variable_item_list_add(app->var_list, "Leak minimum", 4, on_leak_min_change, app);
    variable_item_set_current_value_index(it, app->settings.leak_min_index);
    variable_item_set_current_value_text(it, k_leak_mins[app->settings.leak_min_index]);

    it = variable_item_list_add(app->var_list, "Leak scope", 2, on_leak_scope_change, app);
    variable_item_set_current_value_index(it, app->settings.leak_scope);
    variable_item_set_current_value_text(it, k_leak_scopes[app->settings.leak_scope]);

    view_dispatcher_switch_to_view(app->view_dispatcher, WmbusViewSettings);
}

bool wmbus_view_settings_event(void* ctx, SceneManagerEvent ev) {
    (void)ctx; (void)ev; return false;
}

void wmbus_view_settings_exit(void* ctx) {
    WmbusApp* app = ctx;
    variable_item_list_reset(app->var_list);
}
