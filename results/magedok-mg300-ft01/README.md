# TouchRate measurement — 2026-09-21T23:10:38

**Touch screen:** TouchScreen — `VID_056A  PID_8191` — WingCool Inc.  
**Display:** MG300-FT01 — 1920 × 1080 @ 239.999 Hz nominal, 239.993 Hz measured  
**Session:** 237.0 s, of which 179.0 s with at least one contact

## Headline

| Metric | Value |
| --- | --- |
| Modal report rate | **100.5 Hz** |
| Peak-centred rate | **95.5 Hz** — the centre of the interval peak; see [Report rate, peak-centred](#report-rate-peak-centred) |
| Mean report rate | 95.4 Hz |
| Rate at 1 contact | 99.5 Hz |
| Rate at 10 contacts | 100.5 Hz |
| Interval jitter (sd) | 0.529 ms |
| Worst gap | 21.03 ms |
| Delivery latency p50 / p99 | 1.21 / 1.58 ms |
| Max simultaneous contacts | 10 |
| Render frame rate | 3400 fps mean |

## Report rate by contact count

How the digitizer's report rate changes as fingers are added. Each row covers
the intervals measured while exactly that many contacts were down.

| Contacts | Modal Hz | Mean Hz | Interval ms | Jitter sd ms | Worst gap ms | Intervals | % of 1 contact |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 99.5 | 95.1 | 10.520 | 0.828 | 21.03 | 975 | 100.0% |
| 2 | 100.5 | 95.5 | 10.471 | 0.506 | 12.00 | 1532 | 101.0% |
| 3 | 99.5 | 95.5 | 10.475 | 0.508 | 12.03 | 1122 | 100.0% |
| 4 | 100.5 | 95.5 | 10.477 | 0.501 | 11.09 | 688 | 101.0% |
| 5 | 99.5 | 95.5 | 10.471 | 0.515 | 11.99 | 1113 | 100.0% |
| 6 | 100.5 | 95.5 | 10.473 | 0.504 | 11.99 | 1083 | 101.0% |
| 7 | 99.5 | 95.5 | 10.476 | 0.501 | 12.04 | 2230 | 100.0% |
| 8 | 100.5 | 95.4 | 10.477 | 0.505 | 12.00 | 2476 | 101.0% |
| 9 | 100.5 | 95.4 | 10.478 | 0.505 | 12.03 | 2065 | 101.0% |
| 10 | 100.5 | 95.5 | 10.471 | 0.504 | 11.13 | 3797 | 101.0% |

Best **100.5 Hz** at 2 contacts, worst **99.5 Hz** at 1 contact — a **1% drop**.

## Report rate, peak-centred

The rates above are the most common interval — the tallest step of the interval
histogram — which is how every panel here is measured, so they stay comparable.
That describes a digitizer reporting at one steady interval. One whose reports
land on a coarse tick instead alternates between two: on a 1 ms tick, 95.5
reports a second come out as a mix of 10 ms and 11 ms intervals, and the tallest
step names 100 Hz, a rate the panel never delivers.

Below are the same measurements taken from the centre of the interval peak: the
tallest step together with the neighbouring steps belonging to it, weighted by how
many intervals fell in each. A missed report sits a whole interval further out and
stays excluded, so this is not the plain mean.

The `all` row covers every contact count together. On a panel whose rate falls as
fingers are added, that mixes several rates into one figure and the per-count rows
are what to read.

| Contacts | Modal Hz | Peak-centred Hz | Mean Hz | Peak vs modal |
| ---: | ---: | ---: | ---: | ---: |
| all | 100.5 | **95.5** | 95.4 | -5.0% |
| 1 | 99.5 | **95.4** | 95.1 | -4.1% |
| 2 | 100.5 | **95.5** | 95.5 | -5.0% |
| 3 | 99.5 | **95.5** | 95.5 | -4.1% |
| 4 | 100.5 | **95.5** | 95.5 | -5.0% |
| 5 | 99.5 | **95.5** | 95.5 | -4.0% |
| 6 | 100.5 | **95.5** | 95.5 | -5.0% |
| 7 | 99.5 | **95.5** | 95.5 | -4.1% |
| 8 | 100.5 | **95.4** | 95.4 | -5.0% |
| 9 | 100.5 | **95.4** | 95.4 | -5.0% |
| 10 | 100.5 | **95.5** | 95.5 | -5.0% |

## Report timing

History recovery was **on, so coalesced frames were recovered**.

| Metric | Value |
| --- | --- |
| Input frames | 17088 |
| Samples | 114200 (0 recovered from history) |
| Pointer messages | 17110 |
| Raw HID reports | 28750 |
| Reports per scan | 28750 reports for 17095 scans, 11655 of them continuations |
| Interval mean / sd | 10.4768 / 0.5287 ms |
| Interval min / max | 7.9817 / 21.0347 ms |
| Interval p50 / p90 / p99 / p99.9 | 10.091 / 11.059 / 11.097 / 11.927 ms |

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
| Panel contacts judged (TipSwitch set, started in the window) | 49 |
| Delivered to the app | 49 |
| **Not delivered** | **0** |
| Started on another window, not judged | 0 |
| HID reports: touching / not touching / empty | 28736 / 14 / 0 |
| HID frames / pointer input frames | 17095 / 17088 |
| Window at export | 1920 × 1080 at (0, 0), full screen |
| Edge-swipe blocking at export | in effect |
| Position mapping check | 18.8 px mean offset across 49 matched samples |
| HID descriptor | 5 contact slots; TipSwitch yes; Confidence no; X 0..16383, Y 0..9599 |

### Diagnosis

Every one of the 49 contacts that started in TouchRate's window was delivered
to the app.

## Input delivery latency

Time from the hardware timestamp on a touch report to the moment the application
dequeued it. This is OS and driver delivery only — it excludes panel scan-out and
display processing, so it is a floor for end-to-end latency, not a measurement of it.

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| device → app (ms) | 114200 | 1.106 | 0.379 | 0.069 | 5.494 |

Percentiles: p50 1.207, p90 1.377, p99 1.577, p99.9 3.097 ms

## Multi-touch

Maximum simultaneous contacts observed: **10** of 15 the hardware reports

Contact counts reached: 1✓ 2✓ 3✓ 4✓ 5✓ 6✓ 7✓ 8✓ 9✓ 10✓

Touch downs / ups: 49 / 49

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| interval between downs (ms) | 44 | 4385.51 | 4942.76 | 11.08 | 21559.62 |
| contact dwell, down→up (ms) | 49 | 24402.86 | 24630.07 | 20.01 | 97214.08 |

## OS position processing

Distance between the processed position Windows reports and the raw position from
the digitizer. A non-zero mean means the OS is smoothing or predicting.

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| \|processed − raw\| (px) | 114200 | 0.000 | 0.000 | 0.000 | 0.000 |

Observed himetric range: x 21..21688, y 1..9059 (0.01 mm units).

## Display and rendering

| Property | Value |
| --- | --- |
| Monitor | MG300-FT01 |
| GDI device | `\\.\DISPLAY1` |
| Mode | 1920 × 1080, 32 bpp, DPI 96 (1.00× scale) |
| Nominal refresh | 239.9990 Hz (exact signal timing) |
| DWM composition | 239.9981 Hz (desktop-wide, not this monitor) |
| Measured vblank | 239.9925 Hz (4.1668 ms period, sd 0.0250 ms, 56998 vblanks) |
| Adapter | NVIDIA GeForce RTX 5090 |
| Swap chain | flip-discard, 3 buffers, max frame latency 1 |
| Present mode | immediate (sync interval 0) + allow-tearing |
| Frames rendered | 805047 |
| Frame rate | 3399.7 fps mean, 1946.1 fps 1% low |
| Frame time | mean 0.294 ms, sd 0.545 ms, p99 0.514 ms |
| Presents dropped | n/a in immediate mode |

## Observations

- Modal report rate is **101 Hz** (9.95 ms per report).
- Its intervals do not sit on one step. Measured from the centre of the interval
  peak the rate is **95 Hz** (10.47 ms per report); see the peak-centred section for
  what that means and for the figure at each contact count.
- Interval jitter is low: sd 0.53 ms against a 9.95 ms period.
- Worst gap 21.0 ms stayed within 2.1× the normal period; no report was dropped.
- Report rate holds within 1% across the contact counts measured.
- Median delivery latency 1.21 ms, p99 1.58 ms.
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
