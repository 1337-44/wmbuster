# Driver detection audit — October 2026

Scope: static inspection of the explicit manufacturer/version/medium tables
(`WmbusMVT`) across the drivers in `drivers/europe/`, and targeted
comparison with public [wmbusmeters](https://github.com/wmbusmeters/wmbusmeters)
driver declarations. This is **not a live-air certification** of every model.

## Confirmed issues and fixes

1. **Diehl IZAR/PRIOS** (fixed earlier): several manufacturers put
   nonstandard data in the A-field; mapping only M/V/T to drivers failed
   for real SAP/Oil cases. The PRIOS payload probe now requires a
   checksum-validated decoded marker and billing-date plausibility.
2. **Techem MK-Radio3/4/3a** (fixed earlier): earlier code inverted
   MVT fields and mislabeled medium codes 0x62/0x72 as heat. Reference:
   `wmbusmeters/drivers/src/mkradio{3,4,3a}.xmq`.
3. **Techem MK-Radio4a** (fixed on this branch): TCH 95/37 and HYD FE/06
   were previously routed to the private MK-Radio4 fixed-offset decoder,
   which could report incorrect volumes. Reference:
   `wmbusmeters/drivers/src/mkradio4a.xmq`. They now have a separate
   MK-Radio4a driver that uses OMS/AES handling, rather than private
   MK-Radio4 offsets. This does not invent a value if AES keys are absent.

## Additional review

Inspected explicit MVT tables in manufacturer modules for Apator,
Aquametro, BMeters, Diehl, electricity, Elster, Engelmann, Itron,
Kamstrup, Landis+Gyr, Maddalena, miscellaneous vendors, Qundis,
sensor vendors, Sensus, Sontex, Techem and Zenner.

No additional global inversion pattern was evident in the remaining
tables. However, the following need real telegrams and targeted
upstream comparisons before changing them:

- Generic manufacturer-specific wildcards such as `FF/07` in the
  registry may mask a specialised decoder not yet implemented.
- The Techem FHKV driver deliberately contains alternate `TCH 80/69`
  and `TCH 80/94` signatures. They look nonstandard, but cannot be
  safely removed without real captures.
- Nonstandard/extended medium codes do not necessarily indicate errors:
  some compact-frame vendors reuse these bytes as part of a vendor
  protocol, as demonstrated by IZAR.

When a new meter is ambiguous, capture one `RAW` and `JSON` log record;
compare the frame in wmbusmeters.org/analyze and add a deterministic
regression test before editing MVT signatures.

## Validation required

Run `make -C tests check`, `ufbt`, then test IZAR, Techem MK-Radio3/4,
MK-Radio4a (if available), and unrelated heat meters. GPS on this branch
also requires real UART hardware validation. Do not merge as a
production-ready release until these checks pass.
