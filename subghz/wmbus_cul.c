/* nanoCUL running culfw/modified culfw: ASCII b/bY telegram lines.
 * LPUART is intentionally separate from GPS's USART. */
#include "wmbus_cul.h"
#include "wmbus_cul_proto.h"

#include <furi.h>
#include <furi_hal.h>
#include <string.h>
#include <stdlib.h>

#define CUL_BAUD 38400
#define CUL_RX_CAP 1024
#define CUL_LINE_CAP 600

struct WmbusCul {
    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx;
    char line[CUL_LINE_CAP];
    size_t used;
    bool overflow;
    WmbusMode mode;
};

static void cul_uart_callback(
    FuriHalSerialHandle* serial, FuriHalSerialRxEvent event, void* ctx) {
    WmbusCul* c = ctx;
    if(event != FuriHalSerialRxEventData) return;
    uint8_t byte = furi_hal_serial_async_rx(serial);
    furi_stream_buffer_send(c->rx, &byte, 1, 0);
}

static void cul_command(WmbusCul* c, const char* command) {
    furi_hal_serial_tx(c->serial, (const uint8_t*)command, strlen(command));
}

WmbusCul* wmbus_cul_open(WmbusMode mode) {
    WmbusCul* c = malloc(sizeof(*c));
    if(!c) return NULL;
    memset(c, 0, sizeof(*c));
    c->mode = mode;
    c->rx = furi_stream_buffer_alloc(CUL_RX_CAP, 1);
    if(!c->rx) {
        free(c);
        return NULL;
    }
    /* Do not disable Expansion Modules globally: GPS may be on USART.
     * If LPUART is taken by a system service, report CUL ERR instead.
     * Users can set Settings > Expansion Modules > Listen UART = None. */
    c->serial = furi_hal_serial_control_acquire(FuriHalSerialIdLpuart);
    if(!c->serial) {
        FURI_LOG_E("WmbusCUL", "LPUART busy; check Expansion Modules settings");
        furi_stream_buffer_free(c->rx);
        free(c);
        return NULL;
    }
    furi_hal_serial_init(c->serial, CUL_BAUD);
    furi_hal_serial_async_rx_start(c->serial, cul_uart_callback, c, false);

    /* 'brc' in patched culfw receives C and T; 'brt' T; 'brs' S.
     * C-only and combined modes share the patched firmware's brc command.
     * Sending '\n\r' matches wmbusmeters' culfw serial framing.
     * X21 enables LQI/RSSI trailer bytes on received frames. */
    const char* command =
        mode == WmbusModeS1 ? "brs\n\r" :
        mode == WmbusModeT1 ? "brt\n\r" : "brc\n\r";
    cul_command(c, command);
    furi_delay_ms(30);
    cul_command(c, "X21\n\r");
    FURI_LOG_I("WmbusCUL", "Started nanoCUL 38400 8N1 on LPUART (15/16)");
    return c;
}

void wmbus_cul_close(WmbusCul* c) {
    if(!c) return;
    if(c->serial) {
        cul_command(c, "brx\n\r"); /* stop culfw wM-Bus RX if supported */
        furi_hal_serial_async_rx_stop(c->serial);
        furi_hal_serial_deinit(c->serial);
        furi_hal_serial_control_release(c->serial);
    }
    if(c->rx) furi_stream_buffer_free(c->rx);
    free(c);
}

WmbusCulEvent wmbus_cul_poll(
    WmbusCul* c, uint8_t* frame, size_t cap,
    size_t* frame_len, int8_t* rssi) {
    if(!c || !frame || !frame_len || !rssi) return WmbusCulEventNone;
    *frame_len = 0;
    uint8_t ch = 0;
    if(furi_stream_buffer_receive(c->rx, &ch, 1, 20) != 1) return WmbusCulEventNone;

    do {
        if(ch == '\n' || ch == '\r') {
            if(c->used && !c->overflow) {
                c->line[c->used] = 0;
                c->used = 0;
                if(strcmp(c->line, "TMODE") == 0 ||
                   strcmp(c->line, "CMODE") == 0 ||
                   strcmp(c->line, "SMODE") == 0) {
                    return WmbusCulEventModeReady;
                }
                if(strstr(c->line, "not supported") ||
                   strstr(c->line, "UNKNOWN") ||
                   strstr(c->line, "ERR")) {
                    return WmbusCulEventModeError;
                }
                WmbusCulParseResult r = wmbus_cul_parse_line(
                    c->line, frame, cap, frame_len, rssi);
                if(r == WmbusCulParseFrameA) return WmbusCulEventFrameA;
                if(r == WmbusCulParseFrameB) return WmbusCulEventFrameB;
                if(r == WmbusCulParseBad) return WmbusCulEventBadFrame;
            } else {
                c->used = 0;
            }
            c->overflow = false;
        } else if(!c->overflow) {
            if(c->used < sizeof(c->line) - 1)
                c->line[c->used++] = (char)ch;
            else {
                c->overflow = true;
                c->used = 0;
            }
        }
    } while(furi_stream_buffer_receive(c->rx, &ch, 1, 0) == 1);

    return WmbusCulEventNone;
}
