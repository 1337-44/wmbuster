# NanoCUL GPS Logger — Android USB/OTG

Android 8+ project for **nanoCUL with CH340** running culfw r571.
Connect **Android phone USB-C OTG -> nanoCUL USB**. Flipper Zero is
NOT needed for this capture mode.

## Features

- CH340 and other compatible serial USB devices (USB Host permission).
- Serial: 38400 baud / 8N1 default, manual 57600/115200 option.
- Radio mode selection T+C, T1, C1, S1 and culfw initialization commands:
  V (version), brc (C1/T+C), brt (T1), brs (S1), X21 (RSSI/LQI).
- CUL ASCII b / bY capture, CRC verification, formats A/B, RSSI in dBm.
- Standard unencrypted OMS DIF/VIF values: energy, volume, power,
  temperature, flow, HCA units.
- Conservative handling of proprietary CI (e.g. NLR/B6), missing data,
  unknown formats and encrypted telegrams: RAW logged, NOT fake values.
- Phone GPS or network location: lat/lon, accuracy, fix age and UTC
  timestamps. Location older than 20 seconds is recorded as null.
  Position belongs to PHONE, not the remote meter.
- Foreground service with persistent notification for screen-off capture.
- JSON Lines: one JSON record per telegram (plus rejected frames).
- Export from app through Android's system document picker.
- Offline, no INTERNET permission, no cloud upload.

This is an **MVP**: it does not yet include every specialised meter decoder
from the Flipper wM-buster+ app. Techem and IZAR manufacturer decoders
and AES-128 key handling are future enhancements.

## Install

1. Open this directory with Android Studio and wait for Gradle sync.
2. Build app -> Build APK(s), or download GitHub Actions artifact
   **NanoCulLogger-debug-apk** from Android NanoCUL GPS Logger workflow.
3. Install app-debug.apk on an Android phone with USB OTG host support.
4. Enable location and connect NanoCUL using a suitable USB-C OTG cable.
   Use a powered OTG hub if the phone cannot power the radio board.
5. Tap START; grant precise location and USB permissions.
6. Use 38400 baud and T+C with the patched culfw r571 C1+T1 firmware.
   Other CUL variants may require different modes or baud rates.
7. Wait for CMODE and telegrams. Leave capture running while phone screen
   is off; Android must allow the foreground location service.
8. Tap STOP, then Exportuj log JSONL and choose the destination.

Sample records contain keys timestamp_utc, source, mode, format,
rssi_dbm, wire_hex, frame_hex, manufacturer, id, media, ci,
decode_status, records and location. For unsupported records, records
is empty and raw data still exists.

## Privacy and troubleshooting

- Logs include precise movement data; strip the location field before
  publishing or sharing.
- No GPS fix? Enable location services and allow precise GPS access;
  when position is missing/stale the app logs location: null.
- No USB? Check OTG support, cable, CH340 enumeration and USB permission.
- No telegrams? Check port speed, culfw mode response, power, and antenna.
- C1 mode requires compatible modified culfw firmware. The r571
  implementation with 300-byte serial buffer typically receives telegrams
  up to ~148 bytes; longer telegrams can be truncated at source.
- The build and unit tests are automatically checked by the
  GitHub Actions Android workflow. Test on real Android/CH340 hardware
  is still required before relying on it for unattended field work.

## Project internals

- app/src/main/java/.../CulParser.java — CUL ASCII framing and CRC.
- WmbusDecoder.java — conservative standard OMS decoder.
- NanoCulService.java — USB serial + location + JSONL foreground capture.
- MainActivity.java — user controls, permissions, document export.
- app/src/test/ — offline parser/decoder tests.
