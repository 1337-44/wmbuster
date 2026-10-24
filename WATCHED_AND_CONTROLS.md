# wM-buster+ — watched meters and button shortcuts

Experimental enhancement branch: `feat/watched-meter-big-ui`. Built on the
unmerged BIG/trend and GPS branches. Test on the actual Flipper Zero first.

## App identity and data

The Flipper apps menu now shows **wM-buster+** instead of wM-Buster.
The application ID is **`wmbuster_plus`**, so the official/original
wM-Buster can be installed next to it. The generated FAP should be
`wmbuster_plus.fap`; remove older enhanced `wmbuster.fap` files from
`/ext/apps/Sub-GHz/` if duplicates are confusing.

The enhanced app deliberately retains
`/ext/apps_data/wmbuster/settings.cfg`, `keys.csv` and existing logs.
Watched meters are a separate persistent file
`/ext/apps_data/wmbuster/watched.csv`, one `MMMM,XXXXXXXX` hexadecimal
manufacturer/ID pair per line, max 32 entries.

## Controls and confirmation popups

| Location | Button action | Effect |
| --- | --- | --- |
| Everywhere in app | **Hold OK** | Toggle backlight **ON ↔ AUTO** (popup on every change); starts in AUTO; restores AUTO on exit |
| Meter list / Scan | **Hold Right (>)** | Add/remove highlighted meter to/from Watched; popup **Watching / Unwatched** (or Watch failed if the SD write fails) |
| Meter list / Scan | Short OK | Open selected meter details |
| Meter list / Scan | Short Up/Down | Select another meter |
| Meter details | Short OK | Cycle **Fields → Raw → Big → Fields** |
| Meter details | Short Up/Down | Scroll Fields or Raw |
| Screens | Back | Return to preceding scene |

A watched meter is marked by **one small dot at x=124**, vertically centred
within the meter's row. Dot is black on normal rows and white on the
inverted, selected row. There is no icon, text or additional row.

The two long-press shortcuts show a transient ~1.2 second **Popup** on
screen confirming both the *ON* and *OFF* transitions; afterwards the
original view is restored. Long OK is handled at app level and does not
trigger the short-OK detail transition.

## Automatic BIG when a watched meter is heard

If `watched.csv` has any entries when wM-buster+ opens:

1. The app enters **Scan** automatically (rather than stopping at the
   root menu), so its receiver is ready.
2. On the **first valid link-layer telegram** from any watched meter, it
   selects that meter and switches to **Details → Big**.
3. This automatic navigation occurs **only once per application launch**.
   It does not steal focus every 8 seconds or whenever the meter transmits.
4. A manual choice of a different meter cancels the pending automatic
   jump. Back returns to the normal Scan and root menu.

The BIG view can display current m³ only for a supported decoded water
meter. If the watched meter broadcasts encrypted/unsupported data, BIG
shows `No m3 reading` — never fabricated consumption.

## Leak monitoring scope

Existing settings **Leak detect** Off/On, **Leak window** and
**Leak minimum** remain. The old setting `Leak alarm: Off/On` is replaced
with **Leak scope: All / Watched** (default All).

- **Leak detect = Off**: no leak alert monitoring; short-term water
  history for BIG is still recorded.
- **Leak detect = On; Leak scope = All**: monitor all supported decoded
  water meter telegrams.
- **Leak detect = On; Leak scope = Watched**: monitor ONLY watched meters;
  non-watched meters retain chart history but are not evaluated for
  a leak alert. Switching scope resets leak-continuity state.
- After the existing heuristic signals a suspected leak, the visual
  alert `!LEAK!` and short double sound/vibration alert occur.

This is a heuristic; it is not a certified leak detector or flood alarm.

## BIG layout

- Top 10 px: compact meter identity and `OK fields` (no BIG label).
- Centre: cumulative m³ reading in a **slightly rounded frame with a
  1px bottom/right shadow**.
- Below: left `+... L/h` and right `+... L/min`; both use the same
  **smoothed** estimated rate. At most one decimal in L/min.
- Bottom strip y=53–63: **bar chart spanning x=0–127**. Each bar shows
  the positive change (litres) between successive received meter
  readings, up to the latest 24 telegrams. Height is scaled to the
  largest positive increment in the displayed history; a zero increment
  has no bar.
- A blink near the rate indicates very recent measured flow; `!LEAK!`
  may overlay bars if the leak heuristic activates.
- Rates remain *estimates from quantised radio readings*; small
  fluctuations may remain and do not imply actual reversed flow.

## APP:88 > FW:87 — not removable from FAP source

This is the Flipper firmware's **external-app API version mismatch**
warning, before our application entry point runs: the compiled FAP
requires API 88 but your Flipper firmware advertises API 87.
It **cannot safely be suppressed by changing app name, FAP metadata or
drawing code**.

Use **the same firmware family and SDK/API version** for both the Flipper
and the uFBT build. On Windows, first identify which firmware
(official/Momentum/Unleashed/etc.) and version is installed, then select
a matching SDK or update the actual device firmware to its matching
build. The official troubleshooting instructions are at
https://docs.flipper.net/zero/apps/troubleshoot and official app-dev
instructions at
https://developer.flipper.net/flipperzero/doxygen/app_dev_your_first_app_in_c.html .
Never fake the app API version to bypass loader compatibility checks.

## Build and test

```powershell
git fetch origin
git switch --track origin/feat/watched-meter-big-ui
ufbt
```

Test on the Flipper: add a watched meter by holding Right, exit app,
reopen and verify it starts listening; receive the first telegram, ensure
BIG opens automatically once. Confirm both short and long OK, two
popup states, a single centred marker, persistence, rates in L/h and
L/min, leak scope switching, and that original app remains installable.
Do not merge until `ufbt` and these hardware checks pass.
