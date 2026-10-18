# GPS integration (future work)

This feature is intentionally **not implemented** in the current branch.

- External UART GNSS receiver; avoid blocking the Sub-GHz receive worker.
- Record receive-time UTC (when available), coordinates, fix validity and fix age.
- Leave `RAW` telegram lines unchanged to preserve compatibility with wmbusmeters.org/analyze.
- Add optional location to JSON logs; missing or stale fixes must not emit guessed coordinates.
- Resolve GPIO/UART coexistence with an external CC1101 before implementation.
- Provide a separate persisted `GPS logging` preference and document location privacy.
