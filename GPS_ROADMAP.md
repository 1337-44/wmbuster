# GPS roadmap

Initial UART GPS integration is available on the
`feat/gps-session-and-driver-audit` branch. See [GPS.md](GPS.md) for
wiring, GPS fix detection, NMEA parser, log schema, limitations and testing.

## Later improvements

- Support other UART baud rates selectable in Settings (first release: 9600).
- Automatic detection of external CC1101 and GPS GPIO pin conflicts.
- Optional UART GPS diagnostics screen (raw NMEA, CRC errors, satellites).
- Optional RTC sync to GPS UTC, behind explicit user confirmation.
- Field validation with multiple GNSS breakout boards and long sessions.
- Tests with real moving receivers and delayed/stale GPS packets.
- Optional local-only privacy controls for geographic coordinates.
