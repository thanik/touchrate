# TouchRate measurement — 2026-09-12T14:22:54

**Touch device:** ILITEK-TP — `VID_222A  PID_0001` — ILITEK  
**Display:** RTK FHD HDR  — 1920 × 1080 @ 60.000 Hz nominal, 59.999 Hz measured  
**Session:** 151.7 s, of which 137.3 s with at least one contact

> The rate figures, and the observations that depend on them, were regenerated on
> 2026-09-22 from this export's raw samples, to give the average gap between reports
> rather than the most common one. Everything else is as exported.

## Headline

| Metric | Value |
| --- | --- |
| Report rate | **125.0 Hz** |
| Rate at 1 contact | 125.0 Hz |
| Rate at 10 contacts | 125.0 Hz |
| Gap between reports | steady |
| Mean over every gap | 125.0 Hz |
| Interval jitter (sd) | 0.000 ms |
| Worst gap | 8.00 ms |
| Delivery latency p50 / p99 | 0.65 / 1.19 ms |
| Max simultaneous contacts | 10 |
| Render frame rate | 2871 fps mean |

## Report rate by contact count

How the digitizer's report rate changes as fingers are added. Each row covers
the intervals measured while exactly that many contacts were down. Mean Hz counts
every gap, so it falls below Rate Hz when reports go missing.

| Contacts | Rate Hz | Mean Hz | Interval ms | Jitter sd ms | Worst gap ms | Intervals | % of 1 contact |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 125.0 | 125.0 | 8.000 | 0.000 | 8.00 | 3977 | 100.0% |
| 2 | 125.0 | 125.0 | 8.000 | 0.000 | 8.00 | 1750 | 100.0% |
| 3 | 125.0 | 125.0 | 8.000 | 0.000 | 8.00 | 610 | 100.0% |
| 4 | 125.0 | 125.0 | 8.000 | 0.000 | 8.00 | 1817 | 100.0% |
| 5 | 125.0 | 125.0 | 8.000 | 0.000 | 8.00 | 952 | 100.0% |
| 6 | 125.0 | 125.0 | 8.000 | 0.000 | 8.00 | 961 | 100.0% |
| 7 | 125.0 | 125.0 | 8.000 | 0.000 | 8.00 | 874 | 100.0% |
| 8 | 125.0 | 125.0 | 8.000 | 0.000 | 8.00 | 1078 | 100.0% |
| 9 | 125.0 | 125.0 | 8.000 | 0.000 | 8.00 | 937 | 100.0% |
| 10 | 125.0 | 125.0 | 8.000 | 0.000 | 8.00 | 5472 | 100.0% |

## How the rate is measured

Every rate in this report is the average gap between reports, turned into reports
per second, and it is the figure to compare devices by. Only the normal gaps count:
one far longer than the rest, left by a report that went missing, is not averaged
in. Missed reports show up in the worst gap instead, and in the Mean Hz column,
which counts every gap.

The table below also gives the rate the most common gap alone would suggest. For a
device that keeps the same gap every time, the two agree. For one whose gap varies,
the most common gap is only one of several, and on its own it misstates how many
reports arrive each second.

This panel keeps a steady gap between reports: wherever there are enough intervals to
judge, the two rates agree to within 2%.

| Contacts | Rate Hz | Most common gap ms | That gap alone, Hz | Difference |
| ---: | ---: | ---: | ---: | ---: |
| all | **125.0** | 8.00 | 125.0 | +0.0% |
| 1 | **125.0** | 8.00 | 125.0 | +0.0% |
| 2 | **125.0** | 8.00 | 125.0 | +0.0% |
| 3 | **125.0** | 8.00 | 125.0 | +0.0% |
| 4 | **125.0** | 8.00 | 125.0 | +0.0% |
| 5 | **125.0** | 8.00 | 125.0 | +0.0% |
| 6 | **125.0** | 8.00 | 125.0 | +0.0% |
| 7 | **125.0** | 8.00 | 125.0 | +0.0% |
| 8 | **125.0** | 8.00 | 125.0 | +0.0% |
| 9 | **125.0** | 8.00 | 125.0 | +0.0% |
| 10 | **125.0** | 8.00 | 125.0 | +0.0% |

## Report timing

History recovery was **on, so coalesced frames were recovered**.

| Metric | Value |
| --- | --- |
| Input frames | 18433 |
| Samples | 105002 (0 recovered from history) |
| Pointer messages | 18452 |
| Raw HID reports | 18433 |
| Interval mean / sd | 8.0000 / 0.0000 ms |
| Interval min / max | 8.0000 / 8.0000 ms |
| Interval p50 / p90 / p99 / p99.9 | 8.050 / 8.090 / 8.099 / 8.100 ms |

Raw HID reports read straight from the digitizer come to 1.00× the input
frame count. A ratio near 1.0 means the pointer stack is losing nothing.

## Input delivery latency

Time from the hardware timestamp on a touch report to the moment the application
dequeued it. This is OS and driver delivery only — it excludes panel scan-out and
display processing, so it is a floor for end-to-end latency, not a measurement of it.

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| device → app (ms) | 5 | 0.702 | 0.223 | 0.462 | 1.014 |

Percentiles: p50 0.650, p90 1.100, p99 1.190, p99.9 1.199 ms

## Multi-touch

Maximum simultaneous contacts observed: **10** of 10 the hardware reports

Contact counts reached: 1✓ 2✓ 3✓ 4✓ 5✓ 6✓ 7✓ 8✓ 9✓ 10✓

Touch downs / ups: 118 / 118

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| interval between downs (ms) | 111 | 1307.58 | 3215.50 | 8.00 | 25176.00 |
| contact dwell, down→up (ms) | 118 | 7110.78 | 16178.20 | 16.00 | 89472.00 |

## OS position processing

Distance between the processed position Windows reports and the raw position from
the digitizer. A non-zero mean means the OS is smoothing or predicting.

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| \|processed − raw\| (px) | 105002 | 0.000 | 0.000 | 0.000 | 0.000 |

Observed himetric range: x 2..30927, y 1..17389 (0.01 mm units).

## Display and rendering

| Property | Value |
| --- | --- |
| Monitor | RTK FHD HDR  |
| GDI device | `\\.\DISPLAY2` |
| Mode | 1920 × 1080, 32 bpp, DPI 96 (1.00× scale) |
| Nominal refresh | 60.0000 Hz (exact signal timing) |
| DWM composition | 180.0018 Hz (desktop-wide, not this monitor) |
| Measured vblank | 59.9992 Hz (16.6669 ms period, sd 0.0305 ms, 9105 vblanks) |
| Adapter | NVIDIA GeForce RTX 5090 |
| Swap chain | flip-discard, 3 buffers, max frame latency 1 |
| Present mode | immediate (sync interval 0) + allow-tearing |
| Frames rendered | 435140 |
| Frame rate | 2870.7 fps mean, 1620.0 fps 1% low |
| Frame time | mean 0.348 ms, sd 0.140 ms, p99 0.617 ms |
| Presents dropped | n/a in immediate mode |

## Observations

- Report rate is **125 Hz** (a report every 8.00 ms).
- Interval jitter is low: sd 0.00 ms against a 8.00 ms period.
- Worst gap 8.0 ms stayed within 1.0× the normal period; no report was dropped.
- Report rate does not change with the number of contacts.
- Median delivery latency 0.65 ms, p99 1.19 ms.
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
| Interface path | `\\?\HID#VID_222A&PID_0001&Col01#a&22ea089d&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}` |

### \??\VIRTUAL_DIGITIZER

| Property | Value |
| --- | --- |
| VID / PID | `VID/PID unavailable` |
| Version | `0x0001` |
| Type | Touch screen (HID usage page `0x0D`, usage `0x04`) |
| Max contacts (OS) | 2 |
| Digitizer extents | 67733 × 66675 logical units |
| Mapped display | 2560 × 2520 px at (-330, -1440) |
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
| [`raw/contacts.csv`](raw/contacts.csv) | Per-contact summary |
| [`raw/interval_histogram.csv`](raw/interval_histogram.csv) | Report-interval distribution |
| [`raw/latency_histogram.csv`](raw/latency_histogram.csv) | Delivery-latency distribution |
| [`raw/frametime_histogram.csv`](raw/frametime_histogram.csv) | Render frame-time distribution |

`samples.csv.gz` is gzip-compressed CSV; `pandas.read_csv` and R's `read.csv` open it
directly, as does 7-Zip. All contacts belonging to one hardware report share a `frame_id`
and `device_qpc`. The `from_history` column marks samples recovered from coalesced
frames; their `latency_ms` is not meaningful, so filter on `from_history=0` for
latency analysis.

---

Generated by TouchRate. QPC frequency 10000000 Hz.
