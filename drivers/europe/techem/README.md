# Techem

Many Techem devices ship non-OMS compact frames with fixed offsets and
Techem-private date packing. Those use the decoders in `_compact.{h,c}`.
MK-Radio4a, however, uses an OMS payload and is routed to the OMS
decoder rather than the MK-Radio4 fixed-offset decoder.

## Sources

| Driver | wmbusmeters reference |
|---|---|
| `fhkv_hca.c`       | `src/driver_fhkvdataiii.cc`, `drivers/src/fhkvdataiv.xmq` |
| `mkradio_water.c`  | `src/driver_mkradio3.cc`, `src/driver_mkradio4.cc`, `drivers/src/mkradio4a.xmq` |
| `compact5_heat.c`  | `src/driver_compact5.cc` |
| `vario_heat.c`     | `src/driver_vario451.cc`, `drivers/src/vario451mid.xmq` |
| `smoke.c`          | (private protocol — status bytes only) |

## Known limitations

- The Vario heat decoder uses an integer GJ→kWh approximation
  (raw × 250 / 9). The largest possible 16-bit raw value rounds
  to within ±0.06 kWh of the IEEE-double calculation.
- Smoke alarm bit semantics are not public; we surface the
  status byte verbatim so a non-zero value flags an event.
