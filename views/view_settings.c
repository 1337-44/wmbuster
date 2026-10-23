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

    view_dispatcher_switch_to_view(app->view_dispatcher, WmbusViewSettings);
}

bool wmbus_view_settings_event(void* ctx, SceneManagerEvent ev) {
    (void)ctx; (void)ev; return false;
}

void wmbus_view_settings_exit(void* ctx) {
    WmbusApp* app = ctx;
    variable_item_list_reset(app->var_list);
}
