# TouchRate measurement — 2026-09-13T19:55:25

**Touch device:** ILITEK-TP — `VID_222A  PID_0001` — ILITEK  
**Display:** \\.\DISPLAY1 — 1920 × 1080 @ 60.000 Hz nominal, 59.999 Hz measured  
**Session:** 144.4 s, of which 116.7 s with at least one contact

## Headline

| Metric | Value |
| --- | --- |
| Modal report rate | **124.2 Hz** |
| Mean report rate | 125.0 Hz |
| Rate at 1 contact | 124.2 Hz |
| Rate at 10 contacts | 124.2 Hz |
| Interval jitter (sd) | 0.000 ms |
| Worst gap | 8.00 ms |
| Delivery latency p50 / p99 | 0.20 / 1.19 ms |
| Max simultaneous contacts | 10 |
| Render frame rate | 2967 fps mean |

## Report rate by contact count

How the digitizer's report rate changes as fingers are added. Each row covers
the intervals measured while exactly that many contacts were down.

| Contacts | Modal Hz | Mean Hz | Interval ms | Jitter sd ms | Worst gap ms | Intervals | % of 1 contact |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 124.2 | 125.0 | 8.000 | 0.000 | 8.00 | 1693 | 100.0% |
| 2 | 124.2 | 125.0 | 8.000 | 0.000 | 8.00 | 545 | 100.0% |
| 3 | 124.2 | 125.0 | 8.000 | 0.000 | 8.00 | 398 | 100.0% |
| 4 | 124.2 | 125.0 | 8.000 | 0.000 | 8.00 | 807 | 100.0% |
| 5 | 124.2 | 125.0 | 8.000 | 0.000 | 8.00 | 1624 | 100.0% |
| 6 | 124.2 | 125.0 | 8.000 | 0.000 | 8.00 | 1105 | 100.0% |
| 7 | 124.2 | 125.0 | 8.000 | 0.000 | 8.00 | 1873 | 100.0% |
| 8 | 124.2 | 125.0 | 8.000 | 0.000 | 8.00 | 1137 | 100.0% |
| 9 | 124.2 | 125.0 | 8.000 | 0.000 | 8.00 | 2137 | 100.0% |
| 10 | 124.2 | 125.0 | 8.000 | 0.000 | 8.00 | 4286 | 100.0% |

## Report timing

History recovery was **on, so coalesced frames were recovered**.

| Metric | Value |
| --- | --- |
| Input frames | 15607 |
| Samples | 106257 (0 recovered from history) |
| Pointer messages | 15716 |
| Raw HID reports | 15607 |
| Interval mean / sd | 8.0000 / 0.0000 ms |
| Interval min / max | 8.0000 / 8.0000 ms |
| Interval p50 / p90 / p99 / p99.9 | 8.050 / 8.090 / 8.099 / 8.100 ms |

## Touch delivery

Contacts the panel itself reported, decoded from its raw HID reports, compared
with the pointer contacts Windows delivered to the app. A contact that appears
in the first but not the second never reached the application at all.

| Metric | Value |
| --- | --- |
| Panel contacts (TipSwitch set) | 264 |
| Delivered to the app | 264 |
| **Not delivered** | **0** |
| HID reports: touching / not touching / empty | 15605 / 2 / 0 |
| HID frames / pointer input frames | 15607 / 15607 |
| Window at export | 1920 × 1080 at (0, 0), full screen |
| Edge-swipe blocking at export | in effect |
| Position mapping check | 7.2 px mean offset across 264 matched samples |
| HID descriptor | 10 contact slots; TipSwitch yes; Confidence no; X 0..9600, Y 0..9600 |

### Diagnosis

Every one of the 264 contacts the panel reported was delivered to the app.

## Input delivery latency

Time from the hardware timestamp on a touch report to the moment the application
dequeued it. This is OS and driver delivery only — it excludes panel scan-out and
display processing, so it is a floor for end-to-end latency, not a measurement of it.

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| device → app (ms) | 4 | 0.455 | 0.458 | 0.050 | 1.010 |

Percentiles: p50 0.200, p90 1.120, p99 1.192, p99.9 1.199 ms

## Multi-touch

Maximum simultaneous contacts observed: **10** of 10 the hardware reports

Contact counts reached: 1✓ 2✓ 3✓ 4✓ 5✓ 6✓ 7✓ 8✓ 9✓ 10✓

Touch downs / ups: 264 / 264

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| interval between downs (ms) | 239 | 518.52 | 1079.33 | 8.00 | 10720.00 |
| contact dwell, down→up (ms) | 264 | 3211.91 | 6471.17 | 16.00 | 46824.00 |

## OS position processing

Distance between the processed position Windows reports and the raw position from
the digitizer. A non-zero mean means the OS is smoothing or predicting.

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| \|processed − raw\| (px) | 106257 | 0.000 | 0.000 | 0.000 | 0.000 |

Observed himetric range: x 3..30925, y 2..17387 (0.01 mm units).

## Display and rendering

| Property | Value |
| --- | --- |
| Monitor | \\.\DISPLAY1 |
| GDI device | `\\.\DISPLAY1` |
| Mode | 1920 × 1080, 32 bpp, DPI 120 (1.25× scale) |
| Nominal refresh | 60.0000 Hz (exact signal timing) |
| DWM composition | 180.0018 Hz (desktop-wide, not this monitor) |
| Measured vblank | 59.9993 Hz (16.6669 ms period, sd 0.0277 ms, 8691 vblanks) |
| Adapter | NVIDIA GeForce RTX 5090 |
| Swap chain | flip-discard, 3 buffers, max frame latency 1 |
| Present mode | immediate (sync interval 0) + allow-tearing |
| Frames rendered | 427981 |
| Frame rate | 2966.6 fps mean, 1548.8 fps 1% low |
| Frame time | mean 0.337 ms, sd 0.154 ms, p99 0.646 ms |
| Presents dropped | n/a in immediate mode |

## Observations

- Modal report rate is **124 Hz** (8.05 ms per report).
- Interval jitter is low: sd 0.00 ms against a 8.05 ms period.
- Worst gap 8.0 ms stayed within 1.0× the normal period; no report was dropped.
- Report rate holds within 0% across the contact counts measured.
- Median delivery latency 0.20 ms, p99 1.19 ms.
- The OS applies no measurable smoothing or prediction to reported positions.

Thresholds above are this tool's reporting conventions, not a standard.

## Touch hardware

### ILITEK-TP — active

| Property | Value |
| --- | --- |
| VID / PID | `VID_222A  PID_0001` |
| Version | `0x0002` |
| Manufacturer | ILITEK |
| Type | Touch screen (HID usage page `0x0D`, usage `0x04`) |
| Max contacts (OS) | 10 |
| Max contacts (HID descriptor) | 10 |
| Input report | 64 bytes |
| Digitizer extents | 30931 × 17391 logical units |
| Mapped display | 1920 × 1080 px at (0, 0) |
| Spatial resolution | 16.110 × 16.103 device steps per display pixel |
| Enumerated via | pointer device stack + raw input |
| Interface path | `\\?\HID#VID_222A&PID_0001&MI_00#a&5d4cede&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}` |

### \??\VIRTUAL_DIGITIZER

| Property | Value |
| --- | --- |
| VID / PID | `VID/PID unavailable` |
| Version | `0x0001` |
| Type | Touch screen (HID usage page `0x0D`, usage `0x04`) |
| Max contacts (OS) | 2 |
| Digitizer extents | 54186 × 53340 logical units |
| Mapped display | 2560 × 2520 px at (-313, -1440) |
| Spatial resolution | 21.166 × 21.167 device steps per display pixel |
| Enumerated via | pointer device stack + raw input |
| Interface path | `\\?\VIRTUAL_DIGITIZER` |

System reports touch support: yes. `SM_MAXIMUMTOUCHES` = 10.

## Raw data

Full measurements are in [`raw/`](raw/), and the machine-readable figures in
[`summary.json`](summary.json).

| File | Contents |
| --- | --- |
| [`raw/samples.csv.gz`](raw/samples.csv.gz) | Every buffered sample: timestamps, interval, latency, coordinates, pressure — gzip |
| [`raw/rate_by_contacts.csv`](raw/rate_by_contacts.csv) | Report rate per simultaneous contact count |
| [`raw/undelivered_touches.csv`](raw/undelivered_touches.csv) | Touches the panel reported that Windows did not deliver |
| [`raw/contacts.csv`](raw/contacts.csv) | Per-contact summary |
| [`raw/interval_histogram.csv`](raw/interval_histogram.csv) | Report-interval distribution |
| [`raw/latency_histogram.csv`](raw/latency_histogram.csv) | Delivery-latency distribution |
| [`raw/frametime_histogram.csv`](raw/frametime_histogram.csv) | Render frame-time distribution |

`samples.csv.gz` is gzip-compressed CSV; `pandas.read_csv` and R's `read.csv`
open it directly, as does 7-Zip. All contacts belonging to one hardware report
share a `frame_id` and `device_qpc`. The `from_history` column marks samples
recovered from coalesced frames; their `latency_ms` is not meaningful, so filter
on `from_history=0` for latency analysis.

---

Generated by TouchRate. QPC frequency 10000000 Hz.
