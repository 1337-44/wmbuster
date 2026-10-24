#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define WMBUS_MAX_WATCHED 32
typedef struct { uint16_t manuf; uint32_t id; } WmbusWatched;
typedef struct WmbusApp WmbusApp;
/* Caller holds app->lock for is_watched(); other functions lock internally. */
bool wmbus_is_watched(const WmbusApp* app, uint16_t manuf, uint32_t id);
void wmbus_watched_load(WmbusApp* app);
bool wmbus_watched_toggle(WmbusApp* app, uint16_t manuf, uint32_t id, bool* enabled);
