# GPS UART integration and session logs

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

## Wiring

For a GPS/GNSS receiver that supports **3.3 V power AND 3.3 V UART logic**:

| GPS | Flipper Zero |
|---|---|
| VCC | 3.3 V GPIO pin 9 |
| GND | GND GPIO pin 11 |
| TX | USART RX pin 14 |
| RX (optional) | USART TX pin 13 |

**Do not connect 5 V UART logic directly.** A GPS breakout that requires
5 V power needs suitable external/regulator power according to its datasheet.
Verify a shared ground, voltage level and the pin numbering on YOUR board.
UART pins may conflict with an external CC1101 or other GPIO module; verify
their wiring before enabling both.

UART runs at **9600 baud, 8N1**. NMEA RMC and GGA sentences with valid
checksums are accepted. GPS needs sky visibility and may take minutes to lock.

## Enable and use

1. Update firmware/application and wire the GPS module.
2. Open **Settings**: set **GPS UART = On**.
3. Set **Log to SD = On**, too (GPS On alone does not create logs).
4. Return to scan. Footer reads `GPS no fix` until valid fix,
   `GPS FIX OK` on fix, and `GPS FIX+LOG` shortly after a telegram
   with fresh GPS data was successfully written.
5. If `GPS UART?` appears, the USART port could not be acquired.
   Restart GPS UART after freeing the port.

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
