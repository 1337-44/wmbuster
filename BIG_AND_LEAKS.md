# Big meter display, consumption trends and leak warning (experimental)

This work is developed on `feat/big-meter-trend-leak-alerts` (based on
`feat/gps-session-and-driver-audit`). Its parent GPS branch remains
experimental. Build and test on your Flipper before merging.

## Meter detail screen

Press **OK** to cycle through:

1. **Fields** – existing parsed meter fields, manufacturer, RSSI, packet count.
2. **Raw** – hexadecimal telegram payload.
3. **Big** – large current cumulative consumption (m³), recent average
   consumption rate (L/h), animated `FLOW` indicator when the meter counter
   has increased in the last 20 s, and a tiny 24-sample history graph.
4. Press OK again to return to Fields.

The lower-right `OK raw` / `OK big` / `OK fields` button is now **inverted
(white text on black box)**, with the label baseline shifted 1 LCD pixel
lower; its top horizontal border row has been removed to avoid clipping.

### Backlight always-on (session toggle)

**Hold OK** (until the Flipper long-press event) in any screen of
wM-Buster to toggle the display's backlight lock:

- First long hold: **Always on** (screen does not automatically dim).
- Next long hold: **Auto** (restores system display timeout).
- Only **short OK** cycles Fields / Raw / Big. Long-press repeat events no longer change the view during backlight toggling.
- **Default is Auto** on every app start; this choice is **not persisted**.
- On leaving wM-Buster, the app releases any backlight lock it enabled.
- Keeping the backlight on continuously draws more battery power.

The implementation uses the official Flipper notification sequences
`sequence_display_backlight_enforce_on` /
`sequence_display_backlight_enforce_auto` and listens for exactly
`InputTypeLong + InputKeyOk`; repeat events do not retrigger it.

Big display values update without leaving the meter details screen.
The **full-width 128-pixel bar chart** represents positive water
consumption **increments between received telegrams** (up to 24 readings),
not total consumption or radio strength. The main m³ value appears in
a rounded/shadowed frame; `L/h` and `L/min` share one line, derived
from the same smoothed rate. Quantisation and irregular reception can
still cause fluctuations.
After 60 s without reception the display shows `No recent RX` rather
than a misleading live rate.

Big view is available for decoded **water** meters exposing `Now ... m3`
or `Total ... m3`, including the IZAR and MK-Radio variants already
supported. For unknown, encrypted or other meter formats it shows
`No m3 reading`, rather than a made-up value.

## Leak configuration

Settings >:

| Setting | Available values | Default |
|---|---|---|
| Leak detect | Off / On | Off |
| Leak window | 15, 30, 60, **120** min | 120 min |
| Leak minimum | 1, **5**, 10, 50 L | 5 L |
| Leak scope | All / **Watched** | Watched |

Values persist in `/ext/apps_data/wmbuster/settings.cfg`.
Changing leak detection/window/minimum resets the **leak detection
continuity** without erasing the short-term plot.

This is a deliberately **conservative heuristic** for long-lasting
low-volume continuous consumption, not an alarm for any ordinary use.
Earlier `1 L / 15 min` settings could signal normal household activity.

- At least **6 separate positive meter increments**, seen in **all four
  quarters** of the selected observation window and spanning its full length
- **No more than 12 L/h mean flow** throughout the observation
- Normal bursts (at least **5 L within a 5-minute activity block**) reset
  the suspected-leak candidate, as do counter resets and abnormal jumps
- No positive counter increment for over **a quarter of the window plus
  two minutes** resets the candidate
- **Radio reception gaps greater than two minutes** reset it; no
  transmissions cannot be interpreted as zero consumption
- A candidate must also exceed the configured `Leak minimum` litre value
- Big view shows **`LEAK !!!`** for a suspected leak; with `Leak detect = On`,
  this raises the short double sound/vibration warning once per detection
- `Leak scope = Watched` checks only manually watched meters; `All`
  evaluates every supported decoded water meter
- `Leak detect = Off` disables warnings without disabling BIG history

**Recommended starting configuration:** `Leak detect: On`,
`Leak scope: Watched`, `Leak window: 120 min`, `Leak minimum: 5 L`.
New installations default to that window/volume/scope, while actual
detection stays OFF until enabled. On update, an older `version=1`
configuration with exactly `15 min / 1 L` is automatically migrated to
`120 min / 5 L`; all other previous window/volume choices are retained.
New saves use `version=2`.

**Limitations / caution:**

- This is **not a certified safety or flooding sensor**, and it is not
  sufficient for protecting property against leaks. A closed-loop leak
  detector with its own flow sensor/shutoff is more suitable for that.
- Sampling resolution is constrained by what each meter broadcasts.
  The existing **Techem MK-Radio** driver reports **0.1 m³ = 100 L steps**,
  so small leaks are *not* detectable on a 30-minute horizon from those
  telegrams alone. **IZAR** supports 0.001 m³ = 1 L in its decoded output
  and is substantially more useful here.
- No telegrams means no new measurements; the algorithm cannot infer
  that no water flowed during gaps. Reception must be sufficiently regular.
- Normal recurring consumption (e.g. a running tap, toilet refilling or
  irrigation) can look like a leak. Use the alert as a prompt to inspect,
  not as proof of a fault.
- Water history and leak continuity are **RAM-only** and reset when the
  Flipper application exits or the meter row is evicted.
- The algorithm tracks time based on Flipper monotonic ticks, not RTC,
  so changing the clock or acquiring GPS does not affect detection.

## Test checklist

1. Build using `ufbt`; install the resulting `dist/wmbuster_plus.fap`.
2. Select a known water meter (IZAR / Techem) and open details.
3. Cycle **Fields → Raw → Big → Fields** using OK.
4. Verify the inverted hint text and `m3` reading are legible.
5. With new telegrams arriving, verify the Big reading and sparkline update
   without leaving the detail screen; check `FLOW` on an actual increase.
6. Verify the default leak settings are OFF and configuration persists.
7. Test the detector with **your own** controlled consumption, compare
   real meter increments across the selected window and test gaps/reset.
8. Verify normal everyday usage, bursts, meter resets and radio interruptions
   do not alert; verify the detector needs 6+ increases across all quarters
   of the full observation window, and `Leak detect Off` stays silent.

Host unit tests: `make -C tests test_water_history && ./tests/test_water_history`
(or `make -C tests check` for all tests). They do not replace device tests.
