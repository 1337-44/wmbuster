#pragma once
/* nanoCUL / culfw transport over Flipper LPUART pins TX15 RX16.
 * Intentionally separate from the GPS USART pins TX13 RX14.
 * This module does NOT touch CC1101 registers, GPIO 5V or GPS. */
#include <stdint.h>
#include <stddef.h>
#include "../wmbus_app_i.h"

typedef struct WmbusCul WmbusCul;
typedef enum {
    WmbusCulEventNone = 0,
    WmbusCulEventModeReady,
    WmbusCulEventFrameA,
    WmbusCulEventFrameB,
    WmbusCulEventBadFrame,
    WmbusCulEventModeError,
} WmbusCulEvent;

/* NULL if LPUART occupied or allocation fails. Default baud 38400. */
WmbusCul* wmbus_cul_open(WmbusMode mode);
void wmbus_cul_close(WmbusCul* cul);

/* Wait at most 20 ms for UART bytes. Returns at most one event.
 * CRCs are validated and stripped before the frame is delivered. */
WmbusCulEvent wmbus_cul_poll(
    WmbusCul* cul,
    uint8_t* frame,
    size_t cap,
    size_t* frame_len,
    int8_t* rssi);
