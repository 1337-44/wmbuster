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
- Short OK is unchanged and still cycles Fields / Raw / Big in meter details.
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
| Leak window | 15, **30**, 60, 120 min | 30 min |
| Leak minimum | **1**, 5, 10, 50 L | 1 L |
| Leak scope | All / Watched | All |

Values persist in `/ext/apps_data/wmbuster/settings.cfg`.
Changing leak detection/window/minimum resets the **leak detection
continuity** without erasing the short-term plot.

This is a heuristic to identify **sustained, intermittent small increases
over the chosen duration**, rather than report any one large draw as a leak:

- Minimum **three separate positive increases** are required.
- They must span **at least the selected time window**.
- Gaps between successive increases longer than **half the window**
  (clamped between 2 and 15 minutes) break continuity.
- The sum of positive increases must meet `Leak minimum`.
- Meter rollovers/resets, implausible >10,000 L single jumps, and
  prolonged reception outages reset continuity.
- A confirmed detection displays `!LEAK!` in Big view.
- With **Leak detect = On** and `Leak scope = All`, all supported
  water meters are monitored. With `Leak scope = Watched`, only watched
  water meters are monitored. A short double sound/vibration warning
  fires on the initial suspected-leak transition.
- With **Leak detect = Off**, BIG retains history but leak alarm
  monitoring is disabled.

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
8. Check that no alert occurs without at least 3 increases spanning the
   whole window; verify that `Leak detect Off` is silent.

Host unit tests: `make -C tests test_water_history && ./tests/test_water_history`
(or `make -C tests check` for all tests). They do not replace device tests.
