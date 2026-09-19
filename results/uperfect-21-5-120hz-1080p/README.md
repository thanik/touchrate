# TouchRate measurement — 2026-09-17T22:12:24

**Touch device:** ACDC Touch Input Device — `VID_32D7  PID_0010` — ACDC Co., Ltd.  
**Display:** \\.\DISPLAY2 — 1920 × 1080 @ 120.000 Hz nominal, 120.002 Hz measured  
**Session:** 274.3 s, of which 260.8 s with at least one contact

## Headline

| Metric | Value |
| --- | --- |
| Modal report rate | **83.7 Hz** |
| Mean report rate | 81.7 Hz |
| Rate at 1 contact | 83.7 Hz |
| Rate at 10 contacts | 77.2 Hz |
| Interval jitter (sd) | 0.732 ms |
| Worst gap | 15.00 ms |
| Delivery latency p50 / p99 | 0.26 / 0.59 ms |
| Max simultaneous contacts | 10 |
| Render frame rate | 3350 fps mean |

## Report rate by contact count

How the digitizer's report rate changes as fingers are added. Each row covers
the intervals measured while exactly that many contacts were down.

| Contacts | Modal Hz | Mean Hz | Interval ms | Jitter sd ms | Worst gap ms | Intervals | % of 1 contact |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 83.7 | 85.6 | 11.684 | 0.466 | 12.04 | 3414 | 100.0% |
| 2 | 83.7 | 85.7 | 11.670 | 0.473 | 12.96 | 2275 | 100.0% |
| 3 | 83.7 | 85.5 | 11.690 | 0.469 | 13.01 | 1902 | 100.0% |
| 4 | 83.0 | 84.4 | 11.851 | 0.438 | 13.01 | 2119 | 99.2% |
| 5 | 83.7 | 83.1 | 12.029 | 0.445 | 14.01 | 2375 | 100.0% |
| 6 | 83.7 | 82.2 | 12.165 | 0.383 | 14.00 | 1397 | 100.0% |
| 7 | 76.6 | 79.4 | 12.594 | 0.536 | 14.02 | 1130 | 91.6% |
| 8 | 77.2 | 77.7 | 12.878 | 0.473 | 14.03 | 1259 | 92.3% |
| 9 | 77.2 | 77.0 | 12.979 | 0.368 | 15.00 | 1560 | 92.3% |
| 10 | 77.2 | 76.2 | 13.124 | 0.364 | 15.00 | 3869 | 92.3% |

Best **83.7 Hz** at 1 contact, worst **76.6 Hz** at 7 contacts — a **8% drop**.

## Report timing

History recovery was **on, so coalesced frames were recovered**.

| Metric | Value |
| --- | --- |
| Input frames | 21307 |
| Samples | 113127 (0 recovered from history) |
| Pointer messages | 21434 |
| Raw HID reports | 21314 |
| Interval mean / sd | 12.2448 / 0.7319 ms |
| Interval min / max | 8.9969 / 15.0030 ms |
| Interval p50 / p90 / p99 / p99.9 | 12.041 / 13.057 / 14.032 / 14.095 ms |

## Touch delivery

Contacts the panel itself reported, decoded from its raw HID reports, compared
with the pointer contacts Windows delivered to the app. A contact that appears
in the first but not the second never reached the application at all.

| Metric | Value |
| --- | --- |
| Panel contacts (TipSwitch set) | 583 |
| Delivered to the app | 583 |
| **Not delivered** | **0** |
| HID reports: touching / not touching / empty | 21300 / 7 / 7 |
| HID frames / pointer input frames | 21314 / 21307 |
| Window at export | 1920 × 1080 at (0, 0), full screen |
| Edge-swipe blocking at export | in effect |
| Position mapping check | 19.3 px mean offset across 583 matched samples |
| HID descriptor | 10 contact slots; TipSwitch yes; Confidence no; X 0..32767, Y 0..32767 |

### Diagnosis

Every one of the 583 contacts the panel reported was delivered to the app.

## Input delivery latency

Time from the hardware timestamp on a touch report to the moment the application
dequeued it. This is OS and driver delivery only — it excludes panel scan-out and
display processing, so it is a floor for end-to-end latency, not a measurement of it.

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| device → app (ms) | 113127 | 0.261 | 0.158 | 0.062 | 4.230 |

Percentiles: p50 0.255, p90 0.396, p99 0.589, p99.9 2.390 ms

## Multi-touch

Maximum simultaneous contacts observed: **10** of 10 the hardware reports

Contact counts reached: 1✓ 2✓ 3✓ 4✓ 5✓ 6✓ 7✓ 8✓ 9✓ 10✓

Touch downs / ups: 583 / 583

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| interval between downs (ms) | 525 | 501.65 | 1732.23 | 11.01 | 30284.58 |
| contact dwell, down→up (ms) | 583 | 2430.56 | 6211.92 | 11.99 | 78967.93 |

## OS position processing

Distance between the processed position Windows reports and the raw position from
the digitizer. A non-zero mean means the OS is smoothing or predicting.

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| \|processed − raw\| (px) | 113127 | 0.000 | 0.000 | 0.000 | 0.000 |

Observed himetric range: x 0..52920, y 0..29999 (0.01 mm units).

## Display and rendering

| Property | Value |
| --- | --- |
| Monitor | \\.\DISPLAY2 |
| GDI device | `\\.\DISPLAY2` |
| Mode | 1920 × 1080, 32 bpp, DPI 96 (1.00× scale) |
| Nominal refresh | 120.0000 Hz (exact signal timing) |
| DWM composition | 180.0018 Hz (desktop-wide, not this monitor) |
| Measured vblank | 120.0021 Hz (8.3332 ms period, sd 0.0284 ms, 32983 vblanks) |
| Adapter | NVIDIA GeForce RTX 5090 |
| Swap chain | flip-discard, 3 buffers, max frame latency 1 |
| Present mode | immediate (sync interval 0) + allow-tearing |
| Frames rendered | 918478 |
| Frame rate | 3350.2 fps mean, 1945.0 fps 1% low |
| Frame time | mean 0.298 ms, sd 0.084 ms, p99 0.514 ms |
| Presents dropped | n/a in immediate mode |

## Observations

- Modal report rate is **84 Hz** (11.95 ms per report).
- Interval jitter is low: sd 0.73 ms against a 11.95 ms period.
- Worst gap 15.0 ms stayed within 1.3× the normal period; no report was dropped.
- Report rate holds within 8% across the contact counts measured.
- Median delivery latency 0.26 ms, p99 0.59 ms.
- The OS applies no measurable smoothing or prediction to reported positions.

Thresholds above are this tool's reporting conventions, not a standard.

## Touch hardware

### ACDC Touch Input Device — active

| Property | Value |
| --- | --- |
| VID / PID | `VID_32D7  PID_0010` |
| Version | `0x0009` |
| Manufacturer | ACDC Co., Ltd. |
| Type | Touch screen (HID usage page `0x0D`, usage `0x04`) |
| Max contacts (OS) | 10 |
| Max contacts (HID descriptor) | 10 |
| Input report | 64 bytes |
| Digitizer extents | 53001 × 30001 logical units |
| Mapped display | 1920 × 1080 px at (0, 0) |
| Spatial resolution | 27.605 × 27.779 device steps per display pixel |
| Enumerated via | pointer device stack + raw input |
| Interface path | `\\?\HID#VID_32D7&PID_0010&MI_00&Col01#a&33558109&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}` |

### \??\VIRTUAL_DIGITIZER

| Property | Value |
| --- | --- |
| VID / PID | `VID/PID unavailable` |
| Version | `0x0001` |
| Type | Touch screen (HID usage page `0x0D`, usage `0x04`) |
| Max contacts (OS) | 2 |
| Digitizer extents | 67733 × 66675 logical units |
| Mapped display | 2560 × 2520 px at (-400, -1440) |
| Spatial resolution | 26.458 × 26.458 device steps per display pixel |
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
