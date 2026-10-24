/* SD-backed meter watchlist, independent of volatile RX meter table. */
#include "watched_meters.h"
#include "wmbus_app_i.h"
#include <stdio.h>
#include <string.h>
#define WATCH_PATH WMBUS_APP_DATA_DIR "/watched.csv"

bool wmbus_is_watched(const WmbusApp* app, uint16_t manuf, uint32_t id) {
    for(size_t i=0; i<app->watched_count; i++)
        if(app->watched[i].manuf == manuf && app->watched[i].id == id) return true;
    return false;
}

void wmbus_watched_load(WmbusApp* app) {
    app->watched_count = 0;
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, WATCH_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[32]; size_t n = 0;
        uint8_t b;
        while(storage_file_read(f, &b, 1) == 1) {
            if(b == '\n' || b == '\r') {
                if(n) {
                    line[n] = 0;
                    unsigned manuf = 0; unsigned long id = 0;
                    if(sscanf(line, "%x,%lx", &manuf, &id) == 2 &&
                       manuf <= 0xFFFFu && id <= UINT32_MAX &&
                       !wmbus_is_watched(app, (uint16_t)manuf, (uint32_t)id) &&
                       app->watched_count < WMBUS_MAX_WATCHED) {
                        app->watched[app->watched_count++] =
                            (WmbusWatched){(uint16_t)manuf, (uint32_t)id};
                    }
                }
                n = 0;
            } else if(n < sizeof(line)-1) line[n++] = (char)b;
            else n = 0;
        }
        if(n) {
            line[n] = 0;
            unsigned manuf = 0; unsigned long id = 0;
            if(sscanf(line, "%x,%lx", &manuf, &id) == 2 && manuf <= 0xFFFFu &&
               id <= UINT32_MAX && !wmbus_is_watched(app,(uint16_t)manuf,(uint32_t)id) &&
               app->watched_count < WMBUS_MAX_WATCHED)
                app->watched[app->watched_count++] =
                    (WmbusWatched){(uint16_t)manuf,(uint32_t)id};
        }
        storage_file_close(f);
    }
    storage_file_free(f);
}

bool wmbus_watched_toggle(WmbusApp* app, uint16_t manuf, uint32_t id, bool* enabled) {
    if(!app || !enabled || furi_mutex_acquire(app->lock, 100) != FuriStatusOk) return false;
    bool was = wmbus_is_watched(app, manuf, id);
    if(was) {
        for(size_t i=0;i<app->watched_count;i++) {
            if(app->watched[i].manuf == manuf && app->watched[i].id == id) {
                memmove(&app->watched[i], &app->watched[i+1],
                        (app->watched_count-i-1)*sizeof(WmbusWatched));
                app->watched_count--;
                break;
            }
        }
    } else if(app->watched_count < WMBUS_MAX_WATCHED) {
        app->watched[app->watched_count++] = (WmbusWatched){manuf,id};
    } else {
        furi_mutex_release(app->lock);
        return false;
    }
    *enabled = !was;
    if(app->watched_count == 0) app->auto_watch_pending = false;
    WmbusWatched snap[WMBUS_MAX_WATCHED];
    size_t count = app->watched_count;
    memcpy(snap, app->watched, count*sizeof(*snap));
    furi_mutex_release(app->lock);

    storage_simply_mkdir(app->storage, WMBUS_APP_DATA_DIR);
    File* f = storage_file_alloc(app->storage);
    bool ok = storage_file_open(f, WATCH_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        char line[32];
        for(size_t i=0;i<count;i++) {
            int n=snprintf(line,sizeof(line),"%04X,%08lX\n",
                        (unsigned)snap[i].manuf,(unsigned long)snap[i].id);
            if(n<=0 || storage_file_write(f,line,(size_t)n)!=(size_t)n) {ok=false;break;}
        }
        storage_file_close(f);
    }
    storage_file_free(f);
    return ok;
}
