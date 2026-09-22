# TouchRate measurement — 2026-09-13T14:48:13

**Touch device:** ACDC Touch Input Device — `VID_32D7  PID_0010` — ACDC Co., Ltd.  
**Display:** \\.\DISPLAY2 — 1920 × 1080 @ 120.000 Hz nominal, 119.991 Hz measured  
**Session:** 100.5 s, of which 86.8 s with at least one contact

> The rate figures, and the observations that depend on them, were regenerated on
> 2026-09-22 from this export's raw samples, to give the average gap between reports
> rather than the most common one. Everything else is as exported.

## Headline

| Metric | Value |
| --- | --- |
| Report rate | **58.1 Hz** |
| Rate at 1 contact | 58.1 Hz |
| Rate at 10 contacts | 58.1 Hz |
| Gap between reports | steady |
| Mean over every gap | 58.1 Hz |
| Interval jitter (sd) | 0.419 ms |
| Worst gap | 18.07 ms |
| Delivery latency p50 / p99 | 0.30 / 0.60 ms |
| Max simultaneous contacts | 10 |
| Render frame rate | 3534 fps mean |

## Report rate by contact count

How the digitizer's report rate changes as fingers are added. Each row covers
the intervals measured while exactly that many contacts were down. Mean Hz counts
every gap, so it falls below Rate Hz when reports go missing.

| Contacts | Rate Hz | Mean Hz | Interval ms | Jitter sd ms | Worst gap ms | Intervals | % of 1 contact |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 58.1 | 58.1 | 17.215 | 0.414 | 18.06 | 743 | 100.0% |
| 2 | 58.1 | 58.1 | 17.224 | 0.417 | 18.02 | 487 | 100.0% |
| 3 | 58.0 | 58.0 | 17.230 | 0.422 | 18.03 | 360 | 99.9% |
| 4 | 58.1 | 58.1 | 17.221 | 0.416 | 18.02 | 398 | 100.0% |
| 5 | 58.1 | 58.1 | 17.221 | 0.416 | 18.07 | 416 | 100.0% |
| 6 | 58.1 | 58.1 | 17.223 | 0.417 | 18.02 | 233 | 100.0% |
| 7 | 58.1 | 58.1 | 17.223 | 0.416 | 18.02 | 228 | 100.0% |
| 8 | 58.0 | 58.0 | 17.230 | 0.421 | 18.02 | 552 | 99.9% |
| 9 | 58.0 | 58.0 | 17.232 | 0.424 | 18.04 | 439 | 99.9% |
| 10 | 58.1 | 58.1 | 17.226 | 0.423 | 18.05 | 1182 | 99.9% |

The rate does not change with the number of contacts: 58.0 to 58.1 Hz.

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
| all | **58.1** | 16.99 | 58.9 | -1.3% |
| 1 | **58.1** | 16.99 | 58.9 | -1.3% |
| 2 | **58.1** | 16.99 | 58.8 | -1.3% |
| 3 | **58.0** | 16.99 | 58.9 | -1.4% |
| 4 | **58.1** | 16.99 | 58.9 | -1.3% |
| 5 | **58.1** | 16.99 | 58.9 | -1.3% |
| 6 | **58.1** | 17.01 | 58.8 | -1.2% |
| 7 | **58.1** | 16.99 | 58.8 | -1.3% |
| 8 | **58.0** | 16.99 | 58.9 | -1.4% |
| 9 | **58.0** | 16.99 | 58.9 | -1.4% |
| 10 | **58.1** | 16.99 | 58.9 | -1.4% |

## Report timing

History recovery was **on, so coalesced frames were recovered**.

| Metric | Value |
| --- | --- |
| Input frames | 5039 |
| Samples | 29653 (0 recovered from history) |
| Pointer messages | 5046 |
| Raw HID reports | 5044 |
| Interval mean / sd | 17.2242 / 0.4188 ms |
| Interval min / max | 15.9992 / 18.0680 ms |
| Interval p50 / p90 / p99 / p99.9 | 17.026 / 18.008 / 18.091 / 18.099 ms |

Raw HID reports read straight from the digitizer come to 1.00× the input
frame count. A ratio near 1.0 means the pointer stack is losing nothing.

## Input delivery latency

Time from the hardware timestamp on a touch report to the moment the application
dequeued it. This is OS and driver delivery only — it excludes panel scan-out and
display processing, so it is a floor for end-to-end latency, not a measurement of it.

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| device → app (ms) | 29653 | 0.305 | 0.145 | 0.100 | 3.762 |

Percentiles: p50 0.297, p90 0.497, p99 0.599, p99.9 2.289 ms

## Multi-touch

Maximum simultaneous contacts observed: **10** of 10 the hardware reports

Contact counts reached: 1✓ 2✓ 3✓ 4✓ 5✓ 6✓ 7✓ 8✓ 9✓ 10✓

Touch downs / ups: 18 / 18

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| interval between downs (ms) | 17 | 4449.98 | 3358.77 | 104.01 | 12790.76 |
| contact dwell, down→up (ms) | 18 | 28360.84 | 20489.70 | 326.99 | 62788.93 |

## OS position processing

Distance between the processed position Windows reports and the raw position from
the digitizer. A non-zero mean means the OS is smoothing or predicting.

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| \|processed − raw\| (px) | 29653 | 0.000 | 0.000 | 0.000 | 0.000 |

Observed himetric range: x 0..52394, y 1015..29105 (0.01 mm units).

## Display and rendering

| Property | Value |
| --- | --- |
| Monitor | \\.\DISPLAY2 |
| GDI device | `\\.\DISPLAY2` |
| Mode | 1920 × 1080, 32 bpp, DPI 96 (1.00× scale) |
| Nominal refresh | 120.0000 Hz (exact signal timing) |
| DWM composition | 179.9986 Hz (desktop-wide, not this monitor) |
| Measured vblank | 119.9913 Hz (8.3339 ms period, sd 0.0487 ms, 12062 vblanks) |
| Adapter | NVIDIA GeForce RTX 5090 |
| Swap chain | flip-discard, 3 buffers, max frame latency 1 |
| Present mode | immediate (sync interval 0) + allow-tearing |
| Frames rendered | 354546 |
| Frame rate | 3534.0 fps mean, 2026.0 fps 1% low |
| Frame time | mean 0.283 ms, sd 0.091 ms, p99 0.494 ms |
| Presents dropped | n/a in immediate mode |

## Observations

- Report rate is **58 Hz** (a report every 17.22 ms).
- Interval jitter is low: sd 0.42 ms against a 17.22 ms period.
- Worst gap 18.1 ms stayed within 1.0× the normal period; no report was dropped.
- Report rate does not change with the number of contacts.
- Median delivery latency 0.30 ms, p99 0.60 ms.
- The OS applies no measurable smoothing or prediction to reported positions.

Thresholds above are this tool's reporting conventions, not a standard.

## Touch hardware

### ACDC Touch Input Device — active

| Property | Value |
| --- | --- |
| VID / PID | `VID_32D7  PID_0010` |
| Version | `0x0006` |
| Manufacturer | ACDC Co., Ltd. |
| Type | Touch screen (HID usage page `0x0D`, usage `0x04`) |
| Max contacts (OS) | 10 |
| Max contacts (HID descriptor) | 10 |
| Input report | 64 bytes |
| Digitizer extents | 53001 × 30001 logical units |
| Mapped display | 1920 × 1080 px at (0, 0) |
| Spatial resolution | 27.605 × 27.779 device steps per display pixel |
| Enumerated via | pointer device stack + raw input |
| Interface path | `\\?\HID#VID_32D7&PID_0010&MI_00&Col01#a&19f8681&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}` |

### \??\VIRTUAL_DIGITIZER

| Property | Value |
| --- | --- |
| VID / PID | `VID/PID unavailable` |
| Version | `0x0001` |
| Type | Touch screen (HID usage page `0x0D`, usage `0x04`) |
| Max contacts (OS) | 2 |
| Digitizer extents | 118533 × 38100 logical units |
| Mapped display | 4480 × 1440 px at (-2560, 0) |
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

`samples.csv.gz` is gzip-compressed CSV; `pandas.read_csv` and R's `read.csv`
open it directly, as does 7-Zip. All contacts belonging to one hardware report
share a `frame_id` and `device_qpc`. The `from_history` column marks samples
recovered from coalesced frames; their `latency_ms` is not meaningful, so filter
on `from_history=0` for latency analysis.

---

Generated by TouchRate. QPC frequency 10000000 Hz.
