# TouchRate measurement — 2026-09-22T00:41:38

**Touch screen:** TouchScreen — `VID_056A  PID_8191` — WingCool Inc.  
**Display:** MG300-FT01 — 1920 × 1080 @ 239.999 Hz nominal, 239.987 Hz measured  
**Session:** 214.1 s, of which 195.2 s with at least one contact

## Headline

| Metric | Value |
| --- | --- |
| Modal report rate | **100.5 Hz** |
| Peak-centred rate | **95.5 Hz** — its reports are not evenly spaced; see [Report rate, peak-centred](#report-rate-peak-centred) |
| Mean report rate | 95.5 Hz |
| Rate at 1 contact | 99.5 Hz |
| Rate at 10 contacts | 100.5 Hz |
| Interval jitter (sd) | 0.514 ms |
| Worst gap | 21.02 ms |
| Delivery latency p50 / p99 | 1.17 / 1.57 ms |
| Max simultaneous contacts | 10 |
| Render frame rate | 3333 fps mean |

## Report rate by contact count

How the digitizer's report rate changes as fingers are added. Each row covers
the intervals measured while exactly that many contacts were down.

| Contacts | Modal Hz | Mean Hz | Interval ms | Jitter sd ms | Worst gap ms | Intervals | % of 1 contact |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 99.5 | 95.3 | 10.490 | 0.586 | 21.02 | 1364 | 100.0% |
| 2 | 100.5 | 95.5 | 10.474 | 0.502 | 12.00 | 2147 | 101.0% |
| 3 | 100.5 | 95.5 | 10.475 | 0.502 | 11.11 | 1598 | 101.0% |
| 4 | 100.5 | 95.5 | 10.476 | 0.504 | 11.12 | 1668 | 101.0% |
| 5 | 100.5 | 95.5 | 10.475 | 0.502 | 11.99 | 2302 | 101.0% |
| 6 | 99.5 | 95.5 | 10.477 | 0.504 | 12.00 | 1225 | 100.0% |
| 7 | 100.5 | 95.5 | 10.477 | 0.501 | 11.12 | 1235 | 101.0% |
| 8 | 99.5 | 95.5 | 10.474 | 0.513 | 11.98 | 1101 | 100.0% |
| 9 | 100.5 | 95.4 | 10.483 | 0.509 | 12.11 | 1975 | 101.0% |
| 10 | 100.5 | 95.5 | 10.469 | 0.519 | 13.00 | 4020 | 101.0% |

Best **100.5 Hz** at 2 contacts, worst **99.5 Hz** at 1 contact — a **1% drop**.

## Report rate, peak-centred

The rates above are the most common gap between reports, which is how every panel
here is measured, so they stay comparable. That works for a digitizer that spaces
its reports evenly. Not all of them do: one that times its reports in whole
milliseconds cannot send a report every 10.47 ms, so it alternates — 10 ms, then
11, then 10 again. That is 95.5 reports a second, but the most common gap is
10 ms, and going by that alone would call it 100 Hz, a rate it never delivers.

Below are the same measurements averaged over the gaps that belong together — the
most common one and its neighbours — rather than taken from the most common alone.
A missed report leaves a gap of twice the interval or more, far from the rest, and
stays out of that average, so this is not the plain mean either.

The `all` row covers every contact count together. On a panel whose rate falls as
fingers are added, that mixes several rates into one figure and the per-count rows
are what to read.

| Contacts | Modal Hz | Peak-centred Hz | Mean Hz | Peak vs modal |
| ---: | ---: | ---: | ---: | ---: |
| all | 100.5 | **95.5** | 95.5 | -5.0% |
| 1 | 99.5 | **95.4** | 95.3 | -4.1% |
| 2 | 100.5 | **95.5** | 95.5 | -5.0% |
| 3 | 100.5 | **95.5** | 95.5 | -5.0% |
| 4 | 100.5 | **95.5** | 95.5 | -5.0% |
| 5 | 100.5 | **95.5** | 95.5 | -5.0% |
| 6 | 99.5 | **95.5** | 95.5 | -4.1% |
| 7 | 100.5 | **95.5** | 95.5 | -5.0% |
| 8 | 99.5 | **95.5** | 95.5 | -4.0% |
| 9 | 100.5 | **95.4** | 95.4 | -5.1% |
| 10 | 100.5 | **95.5** | 95.5 | -5.0% |

## Report timing

History recovery was **on, so coalesced frames were recovered**.

| Metric | Value |
| --- | --- |
| Input frames | 18639 |
| Samples | 111419 (0 recovered from history) |
| Pointer messages | 18665 |
| Raw HID reports | 28202 |
| Reports per scan | 28202 reports for 18643 scans, 9559 of them continuations |
| Interval mean / sd | 10.4757 / 0.5137 ms |
| Interval min / max | 2.9996 / 21.0188 ms |
| Interval p50 / p90 / p99 / p99.9 | 10.091 / 11.058 / 11.097 / 11.191 ms |

**This panel splits one scan across several HID reports.** Its report carries 5 contact slots and it has declared up to 10 contacts in one scan,
so the rest follow in continuation reports — HID hybrid mode. Windows assembles them
into one input frame, and every rate here counts scans rather than reports, so they
stay comparable with a panel that fits every contact into one report. `Raw HID
reports` above is the report count, which runs ahead of the scan count whenever more
contacts are down than one report holds.

## Touch delivery

Contacts the panel itself reported, decoded from its raw HID reports, compared
with the pointer contacts Windows delivered to the app. A contact that appears
in the first but not the second never reached the application at all.

Windows delivers a touch to the window under the finger, so only contacts that
started in TouchRate's window are judged. One that started on another window, the
desktop, the taskbar or TouchRate's title bar went there, and is counted separately.

| Metric | Value |
| --- | --- |
| Panel contacts judged (TipSwitch set, started in the window) | 64 |
| Delivered to the app | 64 |
| **Not delivered** | **0** |
| Started on another window, not judged | 0 |
| HID reports: touching / not touching / empty | 28189 / 13 / 0 |
| HID frames / pointer input frames | 18643 / 18639 |
| Window at export | 1920 × 1080 at (0, 0), full screen |
| Edge-swipe blocking at export | in effect |
| Position mapping check | 13.0 px mean offset across 64 matched samples |
| HID descriptor | 5 contact slots; TipSwitch yes; Confidence no; X 0..16383, Y 0..9599 |

### Diagnosis

Every one of the 64 contacts that started in TouchRate's window was delivered
to the app.

## Input delivery latency

Time from the hardware timestamp on a touch report to the moment the application
dequeued it. This is OS and driver delivery only — it excludes panel scan-out and
display processing, so it is a floor for end-to-end latency, not a measurement of it.

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| device → app (ms) | 111419 | 0.985 | 0.469 | 0.063 | 5.253 |

Percentiles: p50 1.167, p90 1.371, p99 1.570, p99.9 3.233 ms

## Multi-touch

Maximum simultaneous contacts observed: **10** of 15 the hardware reports

Contact counts reached: 1✓ 2✓ 3✓ 4✓ 5✓ 6✓ 7✓ 8✓ 9✓ 10✓

Touch downs / ups: 64 / 64

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| interval between downs (ms) | 44 | 4514.00 | 4726.55 | 12.00 | 17316.69 |
| contact dwell, down→up (ms) | 64 | 18226.02 | 27871.42 | 9.00 | 122522.60 |

## OS position processing

Distance between the processed position Windows reports and the raw position from
the digitizer. A non-zero mean means the OS is smoothing or predicting.

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| \|processed − raw\| (px) | 111419 | 0.000 | 0.000 | 0.000 | 0.000 |

Observed himetric range: x 21..21688, y 1..9059 (0.01 mm units).

## Display and rendering

| Property | Value |
| --- | --- |
| Monitor | MG300-FT01 |
| GDI device | `\\.\DISPLAY1` |
| Mode | 1920 × 1080, 32 bpp, DPI 96 (1.00× scale) |
| Nominal refresh | 239.9990 Hz (exact signal timing) |
| DWM composition | 239.9981 Hz (desktop-wide, not this monitor) |
| Measured vblank | 239.9866 Hz (4.1669 ms period, sd 0.0172 ms, 51503 vblanks) |
| Adapter | NVIDIA GeForce RTX 5090 |
| Swap chain | flip-discard, 3 buffers, max frame latency 1 |
| Present mode | immediate (sync interval 0) + allow-tearing |
| Frames rendered | 712953 |
| Frame rate | 3332.8 fps mean, 1926.5 fps 1% low |
| Frame time | mean 0.300 ms, sd 0.085 ms, p99 0.519 ms |
| Presents dropped | n/a in immediate mode |

## Observations

- Modal report rate is **101 Hz** (9.95 ms per report).
- Its reports are not evenly spaced, so the most common gap is not the rate it
  delivers: averaged over the gaps it is **95 Hz** (10.48 ms per report). The
  peak-centred section gives that figure at each contact count.
- Interval jitter is low: sd 0.51 ms against a 10.48 ms period.
- Worst gap 21.0 ms stayed within 2.0× the normal period; no report was dropped.
- Report rate holds within 1% across the contact counts measured.
- Median delivery latency 1.17 ms, p99 1.57 ms.
- The OS applies no measurable smoothing or prediction to reported positions.

Thresholds above are this tool's reporting conventions, not a standard.

## Touch hardware

### TouchScreen — active

| Property | Value |
| --- | --- |
| VID / PID | `VID_056A  PID_8191` |
| Version | `0x0220` |
| Manufacturer | WingCool Inc. |
| Type | Touch screen (HID usage page `0x0D`, usage `0x04`) |
| Max contacts (OS) | 15 |
| Contact slots per HID report | 5 |
| Input report | 58 bytes |
| Digitizer extents | 21691 × 9061 logical units |
| Mapped display | 1920 × 1080 px at (0, 0) |
| Spatial resolution | 11.297 × 8.390 device steps per display pixel |
| Enumerated via | pointer device stack + raw input |
| Interface path | `\\?\HID#VID_056A&PID_8191&MI_00&Col02#a&73982e5&0&0001#{4d1e55b2-f16f-11cf-88cb-001111000030}` |

### \??\VIRTUAL_DIGITIZER

| Property | Value |
| --- | --- |
| VID / PID | `VID/PID unavailable` |
| Version | `0x0001` |
| Type | Touch screen (HID usage page `0x0D`, usage `0x04`) |
| Max contacts (OS) | 2 |
| Digitizer extents | 50800 × 28575 logical units |
| Mapped display | 1920 × 1080 px at (0, 0) |
| Spatial resolution | 26.458 × 26.458 device steps per display pixel |
| Enumerated via | pointer device stack + raw input |
| Interface path | `\\?\VIRTUAL_DIGITIZER` |

### TouchScreen

| Property | Value |
| --- | --- |
| VID / PID | `VID_056A  PID_8191` |
| Version | `0x0220` |
| Manufacturer | WingCool Inc. |
| Type | Pen (HID usage page `0x0D`, usage `0x02`) |
| Max contacts (OS) | 1 |
| Contact slots per HID report | 1 |
| Input report | 27 bytes |
| Digitizer extents | 16384 × 9600 logical units |
| Mapped display | 1920 × 1080 px at (0, 0) |
| Spatial resolution | 8.533 × 8.889 device steps per display pixel |
| Enumerated via | pointer device stack + raw input |
| Interface path | `\\?\HID#VID_056A&PID_8191&MI_00&Col01#a&73982e5&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}` |

System reports touch support: yes. `SM_MAXIMUMTOUCHES` = 15.

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
