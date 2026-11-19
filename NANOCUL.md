# nanoCUL UART receiver for wM-buster+ (experimental)

This is a **fourth receiver choice**, not a replacement for internal or
external CC1101. The existing decoders, keys, watchlist, filtering, alerts
and SD logging are all shared. Only one **wM-Bus RF receiver** is selected
at a time. GPS can continue independently.

## Firmware and serial protocol

This driver targets **nanoCUL / culfw or the improved r568/r571 builds**
on an ATmega328P + CC1101 board, not a generic USB serial radio.
USB/CH340 is **not used**; Flipper Zero is not an ordinary USB host for
CH340.

- Serial interface: **38400 baud, 8N1** (this is independent of the
  avrdude/bootloader upload speed, e.g. 57600 or 115200).
- `T1` mode sends `brt`; `C1` and `T+C` send `brc` (patched culfw
  receives C+T), `S1` sends `brs`.
- It sends `X21` to request RSSI/LQI metadata; `brx` on stop.
- Only serial lines beginning with `b` (format A) or `bY` (format B)
  are decoded. Human-readable firmware responses are ignored except
  mode acknowledgements `TMODE`, `CMODE`, `SMODE`.
- The driver verifies per-block Format A CRCs or final Format B CRC,
  strips CRCs, adjusts the Format B length field, decodes optional
  CC1101 RSSI and forwards the resulting link frames to the existing
  `wmbus_app_on_telegram()` pipeline.
- Firmware with no `brc` support will **not** magically receive mode C.
  Try T1, or update firmware after verifying hardware compatibility.
- **nanoCUL has a firmware/hardware frame limit (often 110 or 148 bytes).**
  It cannot reliably receive all valid 255-byte wM-Bus messages. The
  Flipper parser discards invalid/truncated frames; it does not invent
  missing bytes.

Sources:
- [wmbusmeters nanoCUL guide](https://wmbusmeters.github.io/wmbusmeters-wiki/nanoCUL.html)
- [Flipper expansion UART pin assignments](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/documentation/ExpansionModules.md)

## Wiring: Flipper LPUART, not USB

**Do not connect RX/TX until you have measured the NanoCUL signal level.**

| NanoCUL exposed pad | Flipper Zero | Purpose |
|---|---|---|
| `TX` | **RX pin 16** (LPUART), via 5 V → 3.3 V level shifter if needed | NanoCUL telegrams |
| `RX` | **TX pin 15** (LPUART); shift if needed for NanoCUL input | culfw commands |
| `GND` | GND pin **11** | Common ground |
| `5V` | External regulated supply ONLY if rated for 5 V | Power |
| `3.3V` | **Not connected** until traced/verified | May be regulator output |
| `DTR` | Not connected | USB bootloader reset; unused |

The **6206-3.3** regulator proves only that some circuitry uses 3.3 V.
It does **not** prove the ATmega UART has 3.3 V signals: many Arduino
Nano boards operate at 5 V even with a 3.3 V regulator for CC1101.
If TX idles near 5 V, a level shifter or divider on NanoCUL TX → Flipper
RX is mandatory. Also check input voltage requirements for the
NanoCUL RX line. Never apply 5 V UART logic to Flipper GPIO.

You may power NanoCUL separately (e.g., an isolated battery or its USB
power supply) and connect **GND/TX/RX** after voltage verification.
Do not connect 3.3 V and 5 V supplies to Flipper simultaneously.
The Flipper's GPIO output cannot be assumed to supply all NanoCUL load
without a current/power budget.

### Concurrent GPS

- **nanoCUL**: Flipper LPUART **TX15 / RX16**.
- **NEO-6M or phone/BT GPS bridge**: Flipper USART **TX13 / RX14**.

The two ports are physically distinct. If `CUL ERR` appears, the
system's **Expansion Modules** service may hold LPUART. Disable
`Settings > Expansion Modules > Listen UART` for LPUART before testing.
The application does not take over the GPS serial port or
automatically change the system's expansion configuration.

## Use in wM-buster+

1. Verify NanoCUL voltage levels and attach GND, TX/RX (crossed), power.
2. Close any computer serial terminal attached to the NanoCUL.
3. Start wM-buster+, open **Settings > Module > NanoCUL**.
4. Pick **Mode > T1** for a first test (or **T+C** with patched culfw).
5. Enter **Scan**. Look for `CUL...` (opening/initialising),
   `CUL` (mode accepted or CRC-valid frame), `CUL NO ACK` (LPUART
   successfully opened but no mode response/valid telegram yet), or
   `CUL ERR` (LPUART unavailable/failed). A silent firmware may still
   deliver frames later; after `CUL NO ACK` the receiver keeps listening.
   If NanoCUL works with GPS disabled but not enabled, compare these
   statuses. Both UARTs are separate peripherals; check shared supply
   voltage and serial service ownership rather than assuming the ports
   are physically the same.
6. Set **Log to SD = On** to preserve raw frames in existing session logs.
   The old internal/external CC1101 choices remain available.

Radio `Auto` is unchanged: external CC1101 SPI when available, otherwise
internal. **It intentionally does not auto-select nanoCUL**, because
there is no safe way to infer a connected serial CUL from a free UART
without sending commands to arbitrary hardware.

## Source / tests

- `subghz/wmbus_cul.c`: UART transport, commands and ASCII line collector.
- `subghz/wmbus_cul_proto.c`: independent, CRC-checking culfw parser.
- `subghz/wmbus_worker.c`: new worker branch, shared meter processing.
- `tests/test_cul_proto.c`: sample Format A and Format B packets, metadata,
  malformed/truncated frame rejection.

Host-side parser test (no Flipper required):

```sh
gcc -std=c11 -Wall -Wextra -Werror -I. \
    tests/test_cul_proto.c subghz/wmbus_cul_proto.c \
    protocol/wmbus_crc.c -o /tmp/test_cul
/tmp/test_cul
```

**Status:** source implemented and committed; end-to-end testing with
your particular NanoCUL firmware, baud rate, UART wiring and the current
Flipper firmware is still needed. Tests of internal/external CC1101 and
GPS should also be repeated before merging.
