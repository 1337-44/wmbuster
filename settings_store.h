#pragma once
#include <stdbool.h>
struct WmbusApp;
void wmbus_settings_load(struct WmbusApp* app);
bool wmbus_settings_save(struct WmbusApp* app);
