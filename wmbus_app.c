/*
 * wmbuster — wM-Bus Inspector for Flipper Zero.
 *
 * GUI thread runs the ViewDispatcher; a single worker thread drains the
 * CC1101 RX FIFO, decodes 3-of-6 / NRZ / Manchester, verifies CRC, and
 * hands frames to wmbus_app_on_telegram(). All meter-table mutations are
 * guarded by app->lock; the GUI re-reads on a 500 ms tick.
 */

#include "wmbus_app_i.h"
#include "subghz/wmbus_worker.h"
#include "drivers/engine/driver.h"
#include "protocol/wmbus_aes.h"
#include "key_store.h"
#include "meters_db.h"
#include "logger.h"
#include "gps/gps_receiver.h"
#include "settings_store.h"
#include "views/scan_canvas.h"
#include "views/detail_canvas.h"
#include "views/about_canvas.h"

#include <furi_hal_light.h>
#include <furi_hal_resources.h>
#include <lib/subghz/devices/devices.h>

#include "protocol/wmbus_link.h"
#include "protocol/wmbus_app_layer.h"
#include "protocol/wmbus_manuf.h"
#include "protocol/wmbus_medium.h"

#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <string.h>

#define WMBUS_EVENT_TOGGLE_BACKLIGHT 0xBACE0001UL
#define WMBUS_EVENT_WATCH_BIG        0xBACE0002UL
#define WMBUS_EVENT_POPUP_END        0xBACE0003UL

/* Input service sends this from its own thread; post to the GUI dispatcher
 * so the backlight state is only changed on the application GUI thread.
 * InputTypeLong fires once per hold (Repeat events are ignored). */
static void wmbus_input_backlight_cb(const void* message, void* context) {
    WmbusApp* app = context;
    const InputEvent* event = message;
    if(event->type == InputTypeLong && event->key == InputKeyOk &&
       event->sequence_source == INPUT_SEQUENCE_SOURCE_HARDWARE) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, WMBUS_EVENT_TOGGLE_BACKLIGHT);
    }
}

/* Forward decls from views directory. */
void wmbus_view_meters_enter(void*);
bool wmbus_view_meters_event(void*, SceneManagerEvent);
void wmbus_view_meters_exit(void*);
void wmbus_view_detail_enter(void*);
bool wmbus_view_detail_event(void*, SceneManagerEvent);
void wmbus_view_detail_exit(void*);
void wmbus_view_settings_enter(void*);
bool wmbus_view_settings_event(void*, SceneManagerEvent);
void wmbus_view_settings_exit(void*);
void wmbus_view_keys_enter(void*);
bool wmbus_view_keys_event(void*, SceneManagerEvent);
void wmbus_view_keys_exit(void*);

/* GUI tick: refreshes the meter list when new telegrams have arrived.
 * Pull-based to avoid dispatcher-queue contention from the worker. */
extern void wmbus_view_meters_refresh(WmbusApp* app);
extern void wmbus_view_detail_refresh(WmbusApp* app);
static void on_tick_event(void* ctx) {
    WmbusApp* app = ctx;
    if(app->on_detail_view) {
        /* Live consumption/trend display and short flow animation. */
        wmbus_view_detail_refresh(app);
        return;
    }
    if(!app->on_meters_view) return;
    static uint32_t last_gps_refresh = 0;
    uint32_t now = furi_get_tick();
    WmbusRadioState radio = wmbus_worker_radio_state(app->worker);
    bool radio_changed = app->last_shown_radio_state != (uint8_t)radio;
    if(app->telegram_seq == app->last_shown_seq && !radio_changed &&
        (!app->settings.gps_enabled ||
         (uint32_t)(now - last_gps_refresh) < 1000)) return;
    app->last_shown_seq = app->telegram_seq;
    app->last_shown_radio_state = (uint8_t)radio;
    last_gps_refresh = now;
    wmbus_view_meters_refresh(app);
    if(radio == WmbusRadioUnavailable &&
       app->settings.module == WmbusModuleExternal &&
       !app->radio_error_popup_shown && !app->popup_active) {
        app->radio_error_popup_shown = true;
        wmbus_show_popup(app, "External radio", "Not detected\nCheck CC1101");
    }
}

/* ------------- Start (root menu) scene ------------- */
enum { StartIdxScan = 0, StartIdxSettings, StartIdxKeys, StartIdxAbout };

static void start_pick(void* ctx, uint32_t idx) {
    WmbusApp* app = ctx;
    if(idx == StartIdxScan)          scene_manager_next_scene(app->scene_manager, WmbusSceneMeters);
    else if(idx == StartIdxSettings) scene_manager_next_scene(app->scene_manager, WmbusSceneSettings);
    else if(idx == StartIdxKeys)     scene_manager_next_scene(app->scene_manager, WmbusSceneKeys);
    else if(idx == StartIdxAbout)    scene_manager_next_scene(app->scene_manager, WmbusSceneAbout);
}

/* ------------- About scene ------------- */
static void about_enter(void* ctx) {
    WmbusApp* app = ctx;
    view_dispatcher_switch_to_view(app->view_dispatcher, WmbusViewAbout);
}
static bool about_event(void* ctx, SceneManagerEvent e) { (void)ctx; (void)e; return false; }
static void about_exit(void* ctx) { (void)ctx; }

static void start_enter(void* ctx) {
    WmbusApp* app = ctx;
    /* Keep the worker alive across navigation; only the LED is gated by
     * scene so the user has a visual cue of whether scanning is active. */
    wmbus_scanning_indicator_off(app);
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, WMBUS_APP_NAME);
    submenu_add_item(app->submenu, "Scan",     StartIdxScan,     start_pick, app);
    submenu_add_item(app->submenu, "Settings", StartIdxSettings, start_pick, app);
    submenu_add_item(app->submenu, "Keys",     StartIdxKeys,     start_pick, app);
    submenu_add_item(app->submenu, "About",    StartIdxAbout,    start_pick, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WmbusViewSubmenu);
}

static bool start_event(void* ctx, SceneManagerEvent e) { (void)ctx; (void)e; return false; }
static void start_exit(void* ctx) { WmbusApp* app = ctx; submenu_reset(app->submenu); }

/* ------------- Scene table ------------- */
static void (* const k_on_enter[])(void*) = {
    [WmbusSceneStart]    = start_enter,
    [WmbusSceneMeters]   = wmbus_view_meters_enter,
    [WmbusSceneDetail]   = wmbus_view_detail_enter,
    [WmbusSceneSettings] = wmbus_view_settings_enter,
    [WmbusSceneAbout]    = about_enter,
    [WmbusSceneKeys]     = wmbus_view_keys_enter,
};
static bool (* const k_on_event[])(void*, SceneManagerEvent) = {
    [WmbusSceneStart]    = start_event,
    [WmbusSceneMeters]   = wmbus_view_meters_event,
    [WmbusSceneDetail]   = wmbus_view_detail_event,
    [WmbusSceneSettings] = wmbus_view_settings_event,
    [WmbusSceneAbout]    = about_event,
    [WmbusSceneKeys]     = wmbus_view_keys_event,
};
static void (* const k_on_exit[])(void*) = {
    [WmbusSceneStart]    = start_exit,
    [WmbusSceneMeters]   = wmbus_view_meters_exit,
    [WmbusSceneDetail]   = wmbus_view_detail_exit,
    [WmbusSceneSettings] = wmbus_view_settings_exit,
    [WmbusSceneAbout]    = about_exit,
    [WmbusSceneKeys]     = wmbus_view_keys_exit,
};
static const SceneManagerHandlers k_scene_handlers = {
    .on_enter_handlers  = k_on_enter,
    .on_event_handlers  = k_on_event,
    .on_exit_handlers   = k_on_exit,
    .scene_num          = WmbusScene_Count,
};

/* Show a transient popup for each long-press toggle. Timer completion
 * returns through the GUI dispatcher before switching to the active scene. */
static void wmbus_popup_end(void* context) {
    WmbusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, WMBUS_EVENT_POPUP_END);
}

void wmbus_show_popup(WmbusApp* app, const char* title, const char* text) {
    if(!app || !app->popup) return;
    uint32_t scene = scene_manager_get_current_scene(app->scene_manager);
    app->popup_return_view = scene == WmbusSceneMeters ? WmbusViewScan :
                             scene == WmbusSceneDetail ? WmbusViewDetailCanvas :
                             scene == WmbusSceneSettings ? WmbusViewSettings :
                             scene == WmbusSceneKeys ? WmbusViewDetail :
                             scene == WmbusSceneAbout ? WmbusViewAbout : WmbusViewSubmenu;
    app->popup_active = true;
    popup_reset(app->popup);
    popup_set_header(app->popup, title, 64, 19, AlignCenter, AlignCenter);
    popup_set_text(app->popup, text, 64, 41, AlignCenter, AlignCenter);
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, wmbus_popup_end);
    popup_set_timeout(app->popup, 1200);
    popup_enable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, WmbusViewPopup);
}

/* ------------- view_dispatcher event glue ------------- */
static bool on_custom_event(void* ctx, uint32_t evt) {
    WmbusApp* app = ctx;
    if(evt == WMBUS_EVENT_POPUP_END) {
        if(app->popup_active) {
            app->popup_active = false;
            view_dispatcher_switch_to_view(app->view_dispatcher, app->popup_return_view);
        }
        return true;
    }
    if(evt == WMBUS_EVENT_WATCH_BIG) {
        app->auto_watch_queued = false;
        if(app->on_meters_view && app->selected >= 0) {
            scene_manager_next_scene(app->scene_manager, WmbusSceneDetail);
            detail_canvas_set_big(app->detail_canvas);
        }
        return true;
    }
    if(evt == WMBUS_EVENT_TOGGLE_BACKLIGHT) {
        if(app->popup_active) return true;
        app->backlight_held = !app->backlight_held;
        /* The backlight lock is temporary and applies to this app session.
         * Ordinary OK Short events and navigation remain unchanged. */
        notification_message(
            app->notifications,
            app->backlight_held ? &sequence_display_backlight_enforce_on :
                                  &sequence_display_backlight_enforce_auto);
        FURI_LOG_I("wM-Buster", "Backlight always-on: %s",
                   app->backlight_held ? "ON" : "OFF");
        wmbus_show_popup(app, app->backlight_held ? "Backlight ON" : "Backlight AUTO",
                         app->backlight_held ? "Screen stays on" : "Normal timeout");
        return true;
    }
    /* Forward to the active scene; meters scene uses this to refresh. */
    return scene_manager_handle_custom_event(app->scene_manager, evt);
}

static bool on_back_event(void* ctx) {
    WmbusApp* app = ctx;
    return scene_manager_handle_back_event(app->scene_manager);
}

/* ------------- App lifecycle ------------- */

WmbusApp* wmbus_app_alloc(void) {
    WmbusApp* app = malloc(sizeof(WmbusApp));
    memset(app, 0, sizeof(*app));

    /* Combined T+C is the broadest single-config receiver in the EU. */
    app->settings.mode      = WmbusModeCT;
    app->settings.freq_hz   = 868950000;
    app->settings.logging   = false;
    app->settings.auto_decrypt = true;
    app->settings.sound_enabled = true;
    app->settings.vibration_enabled = true;
    app->settings.led_enabled = true;
    app->settings.gps_enabled = false; /* no UART pin conflicts by default */
    app->settings.leak_enabled = false; /* explicitly opt in */
    app->settings.leak_scope = 1; /* Watched only; Leak detect Off by default */
    app->settings.leak_window_index = 3; /* 120 min: low false-positive default */
    app->settings.leak_min_index = 1; /* 5 L */
    app->settings.module    = WmbusModuleAuto;
    app->selected = -1;

    subghz_devices_init();

    app->lock = furi_mutex_alloc(FuriMutexTypeNormal);
    app->text_buf = furi_string_alloc();

    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    wmbus_settings_load(app);
    wmbus_watched_load(app);
    app->auto_watch_pending = app->watched_count > 0;
    if(app->settings.gps_enabled) app->gps = wmbus_gps_start();

    app->key_store = key_store_alloc(app->storage);
    key_store_load(app->key_store);
    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager   = scene_manager_alloc(&k_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, on_custom_event);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, on_back_event);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, on_tick_event, 500);

    app->submenu = submenu_alloc();
    app->var_list = variable_item_list_alloc();
    app->text_box = text_box_alloc();
    app->popup = popup_alloc();
    app->scan_canvas   = scan_canvas_alloc();
    app->detail_canvas = detail_canvas_alloc();
    app->about_canvas  = about_canvas_alloc();

    view_dispatcher_add_view(app->view_dispatcher, WmbusViewSubmenu,      submenu_get_view(app->submenu));
    view_dispatcher_add_view(app->view_dispatcher, WmbusViewSettings,     variable_item_list_get_view(app->var_list));
    view_dispatcher_add_view(app->view_dispatcher, WmbusViewDetail,       text_box_get_view(app->text_box));
    view_dispatcher_add_view(app->view_dispatcher, WmbusViewPopup,        popup_get_view(app->popup));
    view_dispatcher_add_view(app->view_dispatcher, WmbusViewScan,         scan_canvas_get_view(app->scan_canvas));
    view_dispatcher_add_view(app->view_dispatcher, WmbusViewDetailCanvas, detail_canvas_get_view(app->detail_canvas));
    view_dispatcher_add_view(app->view_dispatcher, WmbusViewAbout,        about_canvas_get_view(app->about_canvas));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    /* Record is held only while wM-Buster runs. Subscribing here covers
     * menus, settings, scan and detail views with the same long-OK key. */
    app->input_events = furi_record_open(RECORD_INPUT_EVENTS);
    app->input_subscription = furi_pubsub_subscribe(
        app->input_events, wmbus_input_backlight_cb, app);

    app->worker = wmbus_worker_alloc(app);
    return app;
}

void wmbus_app_free(WmbusApp* app) {
    if(!app) return;

    /* Stop callbacks before GUI teardown. Never leave a forced-backlight
     * setting behind when exiting to the Flipper main interface. */
    if(app->input_subscription) {
        furi_pubsub_unsubscribe(app->input_events, app->input_subscription);
        app->input_subscription = NULL;
    }
    if(app->input_events) {
        furi_record_close(RECORD_INPUT_EVENTS);
        app->input_events = NULL;
    }
    if(app->backlight_held) {
        notification_message_block(
            app->notifications, &sequence_display_backlight_enforce_auto);
        app->backlight_held = false;
    }
    /* Make sure the LED is off and the radio is parked before we free
     * anything — leaving the cyan blink on after exit was a visible bug. */
    wmbus_scanning_stop(app);
    wmbus_worker_free(app->worker);
    if(app->gps) wmbus_gps_stop(app->gps);

    view_dispatcher_remove_view(app->view_dispatcher, WmbusViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, WmbusViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, WmbusViewDetail);
    view_dispatcher_remove_view(app->view_dispatcher, WmbusViewPopup);
    view_dispatcher_remove_view(app->view_dispatcher, WmbusViewScan);
    view_dispatcher_remove_view(app->view_dispatcher, WmbusViewDetailCanvas);
    view_dispatcher_remove_view(app->view_dispatcher, WmbusViewAbout);

    submenu_free(app->submenu);
    variable_item_list_free(app->var_list);
    text_box_free(app->text_box);
    popup_free(app->popup);
    scan_canvas_free(app->scan_canvas);
    detail_canvas_free(app->detail_canvas);
    about_canvas_free(app->about_canvas);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    if(app->key_store) key_store_free(app->key_store);

    subghz_devices_deinit();

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    furi_string_free(app->text_buf);
    furi_mutex_free(app->lock);
    free(app);
}

/* Scanning helpers: LED is the user-visible "actively listening"
 * indicator and is gated on scene; the worker stays running across
 * scenes so re-entering Scan is instant. */

static void led_on(WmbusApp* app) {
    if(app->led_active || !app->settings.led_enabled) return;
    furi_hal_light_blink_start(LightGreen | LightBlue, 0xC0, 25, 100);
    app->led_active = true;
}

static void led_off(WmbusApp* app) {
    if(!app->led_active) return;
    furi_hal_light_blink_stop();
    furi_hal_light_set(LightGreen | LightBlue, 0);
    app->led_active = false;
}

void wmbus_scanning_start(WmbusApp* app) {
    if(app->worker && !wmbus_worker_is_running(app->worker)) {
        app->radio_error_popup_shown = false;
        wmbus_worker_start(app->worker, app->settings.mode);
    }
    app->scan_alerts_active = true;
    led_on(app);
}

void wmbus_scanning_stop(WmbusApp* app) {
    app->scan_alerts_active = false;
    led_off(app);
    if(app->worker && wmbus_worker_is_running(app->worker)) {
        wmbus_worker_stop(app->worker);
    }
}

void wmbus_scanning_indicator_off(WmbusApp* app) {
    app->scan_alerts_active = false;
    led_off(app);
}
void wmbus_scanning_indicator_on(WmbusApp* app) {
    app->scan_alerts_active = true;
    led_on(app);
}

/* ------------- Telegram callback (worker thread) ------------- */

void wmbus_app_on_telegram(WmbusApp* app, const uint8_t* fr, size_t len, int8_t rssi) {
    if(!fr || len < 10 || len > WMBUS_MAX_FRAME) return;

    WmbusLinkFrame link;
    if(!wmbus_link_parse(fr, len, &link)) return;

    furi_mutex_acquire(app->lock, FuriWaitForever);

    /* Bumped under the lock so the GUI thread always sees a monotonically
     * increasing count even when the meters table is full. */
    app->total_telegrams++;

    /* Snapshot raw frame */
    memcpy(app->last_frame, fr, len);
    app->last_frame_len = len;
    app->last_link = link;

    /* Update meter row */
    WmbusMeter* mt = meters_db_upsert(app, link.M, link.id);
    mt->version = link.version;
    mt->medium  = link.medium;
    mt->rssi    = rssi;
    mt->encrypted = (link.enc_mode != 0);
    mt->decrypted_ok = false;

    /* Encryption mode 5 (AES-CBC, static IV) is the standard for OMS-
     * conformant EU meters. With a matching key we build the link-header
     * IV, decrypt, verify the 2F 2F filler, and hand the cleartext to
     * the OMS walker. Without a key the row keeps the ENC badge. */
    const uint8_t* plain = link.apdu;
    size_t plain_len = link.apdu_len;
    static uint8_t s_plain[WMBUS_MAX_FRAME];
    bool key_found = false, dec_ok = false;
    if(link.enc_mode == 5 && app->key_store && app->settings.auto_decrypt) {
        uint8_t key[16];
        key_found = key_store_lookup(app->key_store, link.M, link.id, key);
        if(key_found) {
            /* Short header (CI 0x7A/5A): AN[1] ST[1] CW[2] CT[..].
             * Long  header (CI 0x72/53/8B): ID[4] Ver[1] Type[1] AN[1] ST[1] CW[2] CT[..].
             */
            size_t hdr_off = 0;
            uint8_t a_field[8];
            switch(link.ci) {
                case 0x7A: case 0x5A:
                    hdr_off = 4;
                    a_field[0] = (uint8_t)(link.id);
                    a_field[1] = (uint8_t)(link.id >> 8);
                    a_field[2] = (uint8_t)(link.id >> 16);
                    a_field[3] = (uint8_t)(link.id >> 24);
                    a_field[4] = link.version;
                    a_field[5] = link.medium;
                    a_field[6] = a_field[7] = 0;
                    break;
                case 0x72: case 0x53: case 0x8B:
                    hdr_off = 10;
                    if(link.apdu_len >= 6) memcpy(a_field, link.apdu, 6);
                    a_field[6] = a_field[7] = 0;
                    break;
                default:
                    hdr_off = 0;
            }
            if(hdr_off > 0 && link.apdu_len > hdr_off) {
                size_t ct_len = link.apdu_len - hdr_off;
                ct_len &= ~(size_t)0x0F; /* round down to 16-byte block */
                if(ct_len >= 16) {
                    WmbusCryptoIvCtx ivx = {
                        .m_field = link.M,
                        .access_no = (uint8_t)link.access_no,
                    };
                    memcpy(ivx.a_field, a_field, 8);
                    uint8_t iv[16];
                    wmbus_aes_build_iv5(&ivx, iv);
                    if(wmbus_aes_cbc_decrypt(
                           key, iv,
                           link.apdu + hdr_off, ct_len, s_plain)) {
                        if(wmbus_aes_verify_2f2f(s_plain, ct_len)) {
                            plain = s_plain + 2; /* skip 2F 2F filler */
                            plain_len = ct_len - 2;
                            dec_ok = true;
                        }
                    }
                }
            }
        }
    }
    mt->have_key = key_found;
    mt->decrypted_ok = dec_ok;

    /* Stage logging payload locally so SD writes happen outside the lock
     * (an SD write can take 100 ms and would otherwise stall the GUI). */
    bool log_request = app->settings.logging;
    static uint8_t s_log_frame[WMBUS_MAX_FRAME];
    static char    s_log_text[sizeof(((WmbusApp*)0)->last_text)];
    static WmbusLinkFrame s_log_link;
    static int8_t  s_log_rssi;
    size_t  log_len = 0;

    /* IZAR/PRIOS can repurpose link version and medium bytes. Use the
     * authenticated PRIOS-header probe before the ordinary MVT fallback. */
    WmbusDecodeCtx ctx = {
        .manuf = link.M, .version = link.version, .medium = link.medium,
        .ci = link.ci, .id = link.id, .apdu = plain, .apdu_len = plain_len,
    };
    const WmbusDriver* drv = wmbus_engine_find_ctx(&ctx);
    mt->is_izar = strcmp(drv->id, "diehl-izar") == 0;
    bool ci_is_oms = false;
    switch(link.ci) {
    case 0x72: case 0x7A: case 0x78: case 0x79:
    case 0x69: case 0x6B: case 0x6F: case 0x73:
    case 0x8A: case 0x8B:
    case 0x53: case 0x5A: case 0x5B:
        ci_is_oms = true; break;
    default:
        ci_is_oms = false;
    }
    app->last_text[0] = 0;
    if(link.enc_mode != 0 && !dec_ok) {
        /* Honest status line — never fabricate values for locked meters. */
        if(!key_found) {
            snprintf(app->last_text, sizeof(app->last_text),
                     "Locked (mode %u)\nNo key on file\nAdd to keys.csv\n",
                     link.enc_mode);
        } else {
            snprintf(app->last_text, sizeof(app->last_text),
                     "Locked (mode %u)\nKey found, decrypt failed\nWrong key?\n",
                     link.enc_mode);
        }
    } else if(plain && plain_len) {
        if(dec_ok) {
            /* Decrypted payload is OMS-standard by definition; manufacturer
             * drivers only apply to legacy unencrypted compact frames. */
            char* o = app->last_text;
            size_t cap = sizeof(app->last_text);
            size_t pos = (size_t)snprintf(o, cap, "Decrypted OK\n");
            wmbus_app_render(plain, plain_len, o + pos, cap - pos);
        } else if(!drv->decode && !drv->decode_ex && !ci_is_oms) {
            /* Non-OMS payload from an unrecognised manufacturer.
             *
             * Emit structured "Label  Value" rows so the meter list and
             * the detail view both render cleanly:
             *
             *   line 1 (title):  "Proprietary CI=XX"
             *   line 2 (summary, becomes mt->last_value):  "Bytes NN"
             *   line 3+ (body):  "Hex00 0102030405060708"
             *
             * Each hex row carries 8 raw bytes = 16 chars, which fits
             * inside the 19-char value buffer used by detail_canvas. The
             * old single-line dump produced one ~50-char run with no
             * whitespace, which (a) spilled into last_value and pushed
             * the manuf/ID off the scan list, and (b) was chopped by the
             * detail-view parser into two rows whose label and value
             * collided on screen.
             */
            /* Mirror the friendly walker-failure layout in driver.c — pure
             * label/value pairs so the detail canvas can render every line
             * as a clean two-column row without overflow. The title line
             * encodes manuf+medium+CI so a developer reading a screenshot
             * has all the routing info to pen a driver. */
            char  code[4]; wmbus_manuf_decode(link.M, code);
            char* o   = app->last_text;
            size_t cap = sizeof(app->last_text);
            size_t pos = (size_t)snprintf(o, cap, "%s %s CI=%02X\n",
                                          code, wmbus_medium_str(link.medium),
                                          link.ci);
            pos += (size_t)snprintf(o + pos, cap - pos,
                                    "Bytes  %u\n"
                                    "Status  proprietary\n"
                                    "Repo  i12bp8/wmbuster\n",
                                    (unsigned)plain_len);
            size_t off = 0;
            int rows = 0;
            while(off < plain_len && rows < 4 && pos + 24 < cap) {
                size_t n = plain_len - off; if(n > 6) n = 6;
                pos += (size_t)snprintf(o + pos, cap - pos,
                                        "Hex%02u ", (unsigned)off);
                for(size_t k = 0; k < n && pos + 3 < cap; k++)
                    pos += (size_t)snprintf(o + pos, cap - pos,
                                            "%02X", plain[off + k]);
                if(pos + 1 < cap) { o[pos++] = '\n'; o[pos] = 0; }
                off += n;
                rows++;
            }
        } else {
            /* Pass the full link context: drivers like Diehl PRIOS need
             * the meter ID and CI byte to derive their stream-cipher seed,
             * and the legacy callback path is preserved for everything
             * else (see wmbus_engine_decode_ctx). */
            wmbus_engine_decode_ctx(&ctx, app->last_text, sizeof(app->last_text));
        }
    }

    /* Snapshot APDU + decoded text into the matching meter row so the
     * Detail view always shows data for the meter the user picked, not
     * whichever meter happened to transmit most recently. */
    mt->ci = link.ci;
    {
        size_t n = plain_len > sizeof(mt->apdu) ? sizeof(mt->apdu) : plain_len;
        if(plain && n) memcpy(mt->apdu, plain, n);
        mt->apdu_len = (uint8_t)n;
    }
    {
        size_t n = sizeof(mt->text) - 1;
        size_t i = 0;
        while(i < n && app->last_text[i]) { mt->text[i] = app->last_text[i]; i++; }
        mt->text[i] = 0;
    }

    /* Short value summary for the meter list.
     *
     * Skip the first line of the decoded text — drivers always emit a
     * descriptive title there ("Heat-cost allocator", "Compact heat",
     * etc.). What the user wants in the list is the next line, which is
     * the actual current reading (e.g. "Now      3 u"). */
    mt->last_value[0] = 0;
    if(app->last_text[0]) {
        const char* p = app->last_text;
        const char* nl = strchr(p, '\n');
        if(nl && *(nl + 1)) p = nl + 1;     /* jump past the title line */
        size_t k = 0;
        while(k < sizeof(mt->last_value) - 1 && p[k] && p[k] != '\n') {
            mt->last_value[k] = p[k]; k++;
        }
        mt->last_value[k] = 0;
    }

    /* Track decoded cumulative WATER volumes only: heat/energy meters
     * must not enter the leak detector. IZAR may advertise SAP Oil. */
    bool leak_triggered = false;
    bool alarm_requested = false;
    bool watch_this = wmbus_is_watched(app, link.M, link.id);
    bool launch_big = app->auto_watch_pending && app->on_meters_view && watch_this;
    if(launch_big) {
        app->auto_watch_pending = false;
        app->auto_watch_queued = true;
        app->selected = (int)(mt - app->meters);
    }
    uint32_t litres;
    bool water_medium = mt->is_izar || link.medium == 0x62 || link.medium == 0x72 ||
                        link.medium == 0x07 || link.medium == 0x06 ||
                        link.medium == 0x15 || link.medium == 0x16 ||
                        link.medium == 0x17;
    if(water_medium && (!mt->encrypted || mt->decrypted_ok) &&
       wmbus_water_parse(app->last_text, &litres)) {
        static const uint16_t k_windows[] = {15, 30, 60, 120};
        static const uint16_t k_min_litres[] = {1, 5, 10, 50};
        uint8_t wi = app->settings.leak_window_index;
        uint8_t mi = app->settings.leak_min_index;
        if(wi > 3) wi = 1;
        if(mi > 3) mi = 0;
        leak_triggered = wmbus_water_update(
            &mt->water, litres, furi_get_tick(),
            k_windows[wi], k_min_litres[mi]);
        bool monitor_here = app->settings.leak_enabled &&
                            (app->settings.leak_scope == 0 || watch_this);
        /* Keep the bar-chart history for every meter, but do not retain
         * leak continuity/alarm state for meters outside selected scope. */
        if(!monitor_here) {
            wmbus_water_reset_leak(&mt->water);
        }
        alarm_requested = monitor_here && leak_triggered;
    }

    if(log_request) {
        log_len = len;
        memcpy(s_log_frame, fr, len);
        memcpy(s_log_text, app->last_text, sizeof(s_log_text));
        s_log_link = link;
        s_log_rssi = rssi;
    }

    furi_mutex_release(app->lock);

    if(launch_big) view_dispatcher_send_custom_event(app->view_dispatcher, WMBUS_EVENT_WATCH_BIG);

    if(log_request && log_len) {
        WmbusGpsFix fix;
        bool sampled = app->settings.gps_enabled &&
                       wmbus_gps_snapshot(app->gps, &fix);
        bool logged = wmbus_log_telegram(
            app->storage, s_log_frame, log_len, s_log_rssi,
            &s_log_link, s_log_text, sampled ? &fix : NULL,
            app->settings.gps_enabled);
        app->gps_last_log_ok = logged && sampled && fix.fix;
        if(app->gps_last_log_ok) app->gps_last_log_tick = furi_get_tick();
    }

    /* Bump the GUI sequence counter so the tick handler redraws. */
    app->telegram_seq++;

    /* A leak warning is explicitly opted in and independent of per-packet
     * notification settings. Emit once when a continuous-flow sequence
     * crosses the configured window, never for an isolated spike. */
    if(alarm_requested && app->notifications) {
        static const NotificationSequence k_leak_alarm = {
            &message_vibro_on, &message_note_c5, &message_delay_50,
            &message_sound_off, &message_vibro_off,
            &message_delay_50,
            &message_vibro_on, &message_note_c5, &message_delay_50,
            &message_sound_off, &message_vibro_off, NULL,
        };
        notification_message(app->notifications, &k_leak_alarm);
    }

    /* Sound and vibration are independent of the cyan scan LED. */
    if(app->scan_alerts_active && app->notifications &&
       (app->settings.sound_enabled || app->settings.vibration_enabled)) {
        static uint32_t last_alert_tick = 0;
        uint32_t now = furi_get_tick();
        if((uint32_t)(now - last_alert_tick) >= 800) {
            last_alert_tick = now;
            static const NotificationSequence k_both = {
                &message_vibro_on, &message_note_c5, &message_delay_50,
                &message_sound_off, &message_vibro_off, NULL
            };
            static const NotificationSequence k_sound = {
                &message_note_c5, &message_delay_50, &message_sound_off, NULL
            };
            static const NotificationSequence k_vibration = {
                &message_vibro_on, &message_delay_50, &message_vibro_off, NULL
            };
            if(app->settings.sound_enabled && app->settings.vibration_enabled) {
                notification_message(app->notifications, &k_both);
            } else if(app->settings.sound_enabled) {
                notification_message(app->notifications, &k_sound);
            } else {
                notification_message(app->notifications, &k_vibration);
            }
        }
    }
}

/* ------------- Entry point ------------- */

int32_t wmbus_app(void* p) {
    (void)p;
    WmbusApp* app = wmbus_app_alloc();
    scene_manager_next_scene(app->scene_manager, WmbusSceneStart);
    /* With saved watched meters, start receiving immediately on launch.
     * The first watched telegram opens BIG once in this session. */
    if(app->auto_watch_pending)
        scene_manager_next_scene(app->scene_manager, WmbusSceneMeters);
    view_dispatcher_run(app->view_dispatcher);
    wmbus_app_free(app);
    return 0;
}
