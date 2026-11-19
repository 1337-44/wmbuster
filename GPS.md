# GPS UART integration and session logs

## GPS source selector and Bluetooth / phone GPS bridge (experimental)

**Settings > GPS source** now has four choices:

| Choice | Where position comes from | Hardware requirement |
|---|---|---|
| Off | No GPS | None |
| UART NEO-6M | NMEA from a directly wired receiver | UART GPS module |
| Phone bridge | Phone GPS to ESP32 Wi-Fi endpoint, then UART NMEA | Original ESP32 + phone-side automation |
| BT GPS bridge | Bluetooth Classic SPP NMEA to ESP32, then UART NMEA | Original ESP32 + SPP GPS |

This is a **transport-selection feature**, not a built-in Flipper Bluetooth
Classic/GATT client. Flipper's stock radio supports BLE, **not Classic
Bluetooth SPP**. Selecting `BT GPS bridge` without a powered/connected
ESP32 does not discover or pair an external GPS. Phone GPS is sent over Wi-Fi
to ESP32, **not directly from Flipper Mobile over BLE**. Both bridge modes
reuse the existing checksum-checked NMEA parser, data aging and SD logging.

The accompanying Arduino sketch:
`extras/esp32_gps_bridge/esp32_gps_bridge.ino`

### Bridge wiring

Use an **original ESP32** (WROOM/WROVER with Bluetooth Classic SPP), not
ESP32-C3/C5/S2/S3. ESP32 **TX2 GPIO17** -> Flipper **USART RX GPIO pin 14**
(3.3 V signal), ESP32 **GND** -> Flipper **GND pin 11**. Power the ESP32
from its own battery or USB. The Flipper only needs RX, so no return TX line.
The ESP32 UART output is **9600 baud 8N1**, matching wM-buster+.

Never connect the ESP32 5 V supply to the Flipper's UART signal input.
Power budget and shared pins must be checked when also attaching an
external CC1101. Only one NMEA source should drive Flipper RX at a time.

### Older battery-powered Bluetooth GPS / Pentagram

Older Pentagram Pathfinder receivers commonly offer Bluetooth Classic SPP
with NMEA, but the exact **P3303** model's radio profile and PIN still need
verification. Pair it first with an Android phone/PC to see whether it
exposes `Serial Port (SPP)` and sends RMC/GGA sentences.

1. In the sketch select `#define BRIDGE_MODE MODE_BT_SPP`.
2. Replace `gpsMac[]` with the receiver's **BT MAC address**.
3. Check `gpsPin` (often `0000` or `1234`, depends on model).
4. Upload to original ESP32 using Arduino-ESP32 with BluetoothSerial.
5. Open the ESP32 USB Serial Monitor **115200 baud**, power the GPS, and
   verify `BT GPS connected`. If it fails, verify MAC, PIN, SPP profile,
   and battery. This is not a tested P3303-specific driver.
6. In wM-buster+ choose **GPS source > BT GPS bridge**. The scan screen
   should report `GPS FIX OK` once valid NMEA positions are received.

### Phone coordinates without direct BLE support

The sample sketch also provides a Wi-Fi transport for **Android phone GPS**:

1. Change `BRIDGE_MODE` to `MODE_PHONE_WIFI`; change the default
   `apPassword` before uploading to the ESP32.
2. Connect the phone to ESP32 Wi-Fi network `wmGPS-Bridge`. ESP32 AP
   address is `192.168.4.1`.
3. Use a phone automation app capable of reading the phone's **fresh**
   location and issuing an HTTP GET once every 1–3 s to:
   `http://192.168.4.1/fix?lat=51.751&lon=19.451` — **example/test
   coordinates only**. Substitute current decimal latitude/longitude from
   the phone; Android Tasker is one possible sender. Sending this static
   example will intentionally report a fake test position.
4. In wM-buster+ choose **GPS source > Phone bridge**. Valid updates are
   converted into checksummed `$GPGGA` frames and forwarded over UART.
   Unknown satellite count, HDOP, altitude and UTC date are left unknown;
   the bridge does not pretend to know them. On interruption, the Flipper
   expires the received fix rather than recording old coordinates.

**Security:** The Wi-Fi endpoint carries precise location in plaintext
within the ESP32 access point. Use a strong, private AP password and do not
expose the endpoint to the internet. Phone location requires phone-side
permissions, and mobile OS background restrictions can interrupt updates.
No phone-side automation or app is bundled in this repository.

### Status messages

`GPS no UART` means no bytes reached Flipper RX in the last 10 s.
`GPS NMEA err` means bytes arrived but no valid RMC/GGA; check 9600 baud
and that the bridge sent complete NMEA lines with checksums.
`GPS no fix` means valid GPS sentences without a current position.
`GPS FIX OK` means a current position was accepted.
`GPS UART busy` means the Flipper's USART couldn't be opened.

**Development status:** Source and sketch have not yet been compiled or
tested against real Pentagram P3303 hardware or the user's phone. The
Bluetooth and phone transports require external bridge hardware; they are
not direct wireless Flipper connections.


Experimental GPS support for wM-Buster / Flipper Zero. The radio receiver
remains receive-only and works without any GPS module connected.

## Sources and licensing

- UART asynchronous receiver design adapted from
  [ezod/flipperzero-gps](https://github.com/ezod/flipperzero-gps) (GPL-3.0).
- NMEA 0183 parsing: [kosma/minmea](https://github.com/kosma/minmea)
  by Kosma Moczek, WTFPL version 2. Sources in `gps/minmea.{c,h}`;
  full notice in `gps/COPYING.minmea`. `minmea_gettime()` is not used;
  its host-only `timegm` call is substituted for Flipper toolchain compatibility.
- Independent, adapted integration in `gps/gps_receiver.{c,h}`.

## Wiring and 5 V GPIO power

For **NEO-6M breakout boards with an onboard 5 V-to-3.3 V regulator**
(and **3.3 V UART logic**), use:

| GPS module | Flipper Zero |
|---|---|
| VCC | 5 V GPIO **pin 1** |
| GND | GND GPIO pin 11 |
| TX | USART RX pin 14 |
| RX (optional) | USART TX pin 13 |

**Important:** 5 V is only suitable for breakout boards *rated for 5 V on
VCC*. The bare NEO-6M chip itself uses 3.3 V. For modules specifically
rated for a 3.3 V input, use **GPIO pin 9** as VCC instead.

Unlike the 3.3 V pin, Flipper's 5 V pin 1 is normally switched off. Since
the GPS test application enables that output, earlier wM-Buster builds
appeared to show 'GPS no fix' and an unpowered LED on 5 V NEO-6M boards.
The fixed GPS integration **automatically enables GPIO 5 V on GPS UART On**,
and disables it on GPS UART Off or app exit **only if this GPS instance
enabled it**. If the supply was already on, the app leaves it on. A failed
5 V enable is logged as a warning; confirm wiring and test the 5 V GPIO
output if the GPS still receives no power.

**Never feed 5 V TTL signals into UART GPIO.** GPS TX must use 3.3 V
logic or a level shifter. Confirm voltage ratings and pin numbers for
your exact breakout before wiring. UART pins may conflict with external
CC1101 or other modules; verify coexistence.

UART runs at **9600 baud, 8N1**. NMEA RMC/GGA messages require valid
checksums; GPS needs sky visibility and may take minutes to lock.

## Enable and use

1. Update firmware/application and wire the GPS module.
2. Open **Settings**: set **GPS UART = On**.
3. Set **Log to SD = On**, too (GPS On alone does not create logs).
4. Return to scan. Footer reads `GPS no fix` until valid fix,
   `GPS FIX OK` on fix, and `GPS FIX+LOG` shortly after a telegram
   with fresh GPS data was successfully written.
5. If `GPS UART busy` appears, the USART handle could not be acquired.
   The app now temporarily disables the system Expansion Modules service
   before opening USART and restores it after closing GPS. If USART is
   still busy because of some other service, switch GPS UART Off then On,
   and close other UART apps. Alternatively, test with the system setting
   **Settings > Expansion Modules > Listen UART > None**.
6. `GPS no fix` is a different situation: the UART handle is present,
   but no fresh valid GPS RMC position has been parsed yet. If another
   GPS app can fix quickly on this wiring, inspect baud rate and NMEA
   reception rather than leaving the GPS outdoors indefinitely.

The preference is stored at `/ext/apps_data/wmbuster/settings.cfg`.
GPS defaults to Off for existing installations.

## File names and log fields

Old `session_XXXX.log` names were based on `furi_get_tick()` (uptime tick
count), **not** on a date. New files use the Flipper device RTC **local clock**
when valid, e.g. `session_2026-10-18_22-03-42.log`. If a file with that
name already exists, suffix `_001`, `_002`, etc. is used; if RTC is
uninitialised, `session_boot_<ticks>.log` is used instead.

The `RAW` telegram line is unchanged, so raw capture can still be copied
to wmbusmeters.org/analyze. Each telegram's JSON line now includes:

- `device_time`: local Flipper clock, without timezone suffix (set the RTC).
- `tick_ms`: device uptime sample (relative timing).
- `gps_enabled`: whether GPS UART was requested.
- `gps: null` when disabled, no fix, stale fix (>5 seconds), or unavailable.
- On a valid fix: `gps.fix`, `gps.utc` (UTC estimated for this telegram,
  rounded to whole seconds from recent RMC and its age), `gps.lat`, `gps.lon`,
  `gps.speed_kmh`, `gps.speed_knots`, `gps.course_deg`, `gps.age_ms`.
- When recent GGA is available: `gps.fix_quality`, `gps.satellites`,
  `gps.hdop`, `gps.altitude_m`.

The GPS time is UTC. The filename and `device_time` use Flipper RTC, and
may differ from UTC. The position represents **where the Flipper was when
receiving**, not the location of the remote water meter. GPS logs contain
precise movement data: avoid publishing raw logs with personal coordinates.

## Testing before merging

Build/check on Flipper firmware with `ufbt`, test both `GPS UART Off`
and `On`, verify checksum rejection / fresh RMC / stale fix suppression,
validate every JSON line, confirm `GPS FIX+LOG` only after successful SD
writes, and test GPS together with the selected internal/external CC1101.


## Diagnosing `GPS no fix` after changing firmware

If an independent NMEA application shows a satellite fix but wM-buster+
previously displayed `GPS no fix`, check the scanner footer after
updating this branch:

- `GPS UART busy`: USART could not be acquired from the firmware.
- `GPS no UART`: no raw UART bytes arrived in the past ten seconds;
  check GPS power, RX/TX wiring, Expansion Modules ownership and GPS baud.
  wM-buster+ currently uses 9600 baud on Flipper USART.
- `GPS NMEA err`: UART bytes arrived but no checksum-valid RMC/GGA
  sentence in the past ten seconds; compare GPS app baud and raw NMEA.
- `GPS no fix`: checksum-valid position-related NMEA arrives but neither
  recent valid RMC nor quality-verified GGA reports a position.
- `GPS FIX OK`: recent valid RMC or quality-verified GGA with coordinates.
  If only GGA is valid, positional fix is accepted without inventing a
  complete calendar date: `utc_valid` remains false for geotagging.

This eliminates the previous overstrict requirement that every GPS
position must also have a valid full RMC UTC date. It does not remove
NMEA checksum validation or use stale positions.
