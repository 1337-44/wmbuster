/* Detail scene: render the selected meter on the DetailCanvas widget. */

#include "../wmbus_app_i.h"
#include "../protocol/wmbus_manuf.h"
#include "../protocol/wmbus_medium.h"
#include "../drivers/engine/driver.h"
#include "detail_canvas.h"
#include <stdio.h>
#include <string.h>

static const char* skip_ws(const char* p) {
    while(*p == ' ' || *p == '\t') p++;
    return p;
}

static const char* take_word(const char* p, char* out, size_t cap) {
    p = skip_ws(p);
    size_t k = 0;
    while(*p && *p != ' ' && *p != '\t' && *p != '\n' && k < cap - 1) out[k++] = *p++;
    out[k] = 0;
    return p;
}

static const char* take_rest(const char* p, char* out, size_t cap) {
    p = skip_ws(p);
    size_t k = 0;
    while(*p && *p != '\n' && k < cap - 1) out[k++] = *p++;
    while(k > 0 && (out[k-1] == ' ' || out[k-1] == '\t')) k--;
    out[k] = 0;
    return p;
}

static bool keep_row(const char* label, const char* value) {
    if(!*value) return false;
    if((!strcmp(label, "Rad") || !strcmp(label, "Room")) && !strcmp(value, "0.00 C"))
        return false;
    return true;
}

/* Driver text is "Title\nLabel  Value\nLabel  Value\n..."
 * The first line is the title (rendered in the header subtitle), so we
 * skip it and treat each subsequent line as one row. */
static size_t parse_rows(const char* text, DetailRow* rows, size_t cap) {
    if(!text || !*text) return 0;
    const char* nl = strchr(text, '\n');
    if(nl && *(nl + 1)) text = nl + 1;

    size_t n = 0;
    const char* p = text;
    while(*p && n < cap) {
        DetailRow row;
        p = take_word(p, row.label, sizeof(row.label));
        if(!*row.label) { if(*p == '\n') p++; continue; }
        p = take_rest(p, row.value, sizeof(row.value));
        if(*p == '\n') p++;
        if(keep_row(row.label, row.value)) rows[n++] = row;
    }
    return n;
}

void wmbus_view_detail_enter(void* ctx) {
    WmbusApp* app = ctx;

    WmbusMeter mt;
    bool valid = false;
    if(furi_mutex_acquire(app->lock, 50) == FuriStatusOk) {
        if(app->selected >= 0 && (size_t)app->selected < app->meter_count) {
            mt = app->meters[app->selected];
            valid = true;
        }
        furi_mutex_release(app->lock);
    }
    if(!valid) {
        detail_canvas_set_header(app->detail_canvas, "(no meter)", "", 0, 0, false, "");
        detail_canvas_set_rows(app->detail_canvas, NULL, 0);
        view_dispatcher_switch_to_view(app->view_dispatcher, WmbusViewDetailCanvas);
        return;
    }

    char m[4]; wmbus_manuf_decode(mt.manuf, m);
    const WmbusDriver* driver = wmbus_engine_find(mt.manuf, mt.version, mt.medium);
    const char* model = wmbus_engine_model_name(mt.manuf, mt.version, mt.medium);
    const char* med   = wmbus_medium_str(mt.medium);

    /* Techem MK-Radio uses 0x62 for warm and 0x72 for cold water.
     * The full model title is too long for the narrow details header:
     * keep a short model + temperature label, and show the full
     * water type as the first data row (so it cannot be truncated). */
    const char* mk_model = NULL;
    if(strcmp(driver->id, "techem-mkradio4") == 0) mk_model = "MK4";
    else if(strcmp(driver->id, "techem-mkradio3") == 0) mk_model = "MK3";
    else if(strcmp(driver->id, "mkradio3a") == 0) mk_model = "MK3a";
    bool mk_water = mk_model && (mt.medium == 0x62 || mt.medium == 0x72);
    const char* water_type = mt.medium == 0x62 ? "Hot water" : "Cold water";

    char id_line[24];
    snprintf(id_line, sizeof(id_line), "%s %08lX", m, (unsigned long)mt.id);

    /* The id line above already shows the manufacturer code, so the
     * subtitle is just the model (when known) or the medium type. */
    char subtitle[28];
    if(mt.is_izar)   snprintf(subtitle, sizeof(subtitle), "Diehl IZAR water");
    else if(mk_water) snprintf(subtitle, sizeof(subtitle), "%s %s",
                               mk_model, mt.medium == 0x62 ? "Hot" : "Cold");
    else if(model)   snprintf(subtitle, sizeof(subtitle), "%s", model);
    else if(med)     snprintf(subtitle, sizeof(subtitle), "%s", med);
    else             subtitle[0] = 0;

    const char* badge = "";
    if(mt.encrypted) {
        if(mt.decrypted_ok)  badge = "DEC";
        else if(mt.have_key) badge = "BAD";
        else                 badge = "ENC";
    }
    detail_canvas_set_header(app->detail_canvas, id_line, subtitle,
                             mt.rssi, mt.telegram_count,
                             mt.encrypted, badge);

    DetailRow rows[16];
    size_t n = 0;
    if(mk_water) {
        memset(&rows[0], 0, sizeof(rows[0]));
        snprintf(rows[0].label, sizeof(rows[0].label), "Type");
        snprintf(rows[0].value, sizeof(rows[0].value), "%s", water_type);
        n = 1;
    }
    n += parse_rows(mt.text, rows + n, 16 - n);
    detail_canvas_set_rows(app->detail_canvas, rows, n);
    /* Pass the most recent APDU so the user can press OK to flip into
     * raw-bytes mode for forensics or RE work. */
    detail_canvas_set_raw(app->detail_canvas, mt.apdu, mt.apdu_len);

    view_dispatcher_switch_to_view(app->view_dispatcher, WmbusViewDetailCanvas);
}

bool wmbus_view_detail_event(void* ctx, SceneManagerEvent ev) {
    (void)ctx; (void)ev; return false;
}

void wmbus_view_detail_exit(void* ctx) { (void)ctx; }
