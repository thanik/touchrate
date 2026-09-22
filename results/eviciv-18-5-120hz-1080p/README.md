# TouchRate measurement — 2026-09-12T13:34:44

**Touch device:** TouchScreen — `VID_27C6  PID_0529` — WingCool Inc.  
**Display:** HG585J32 — 1920 × 1080 @ 120.000 Hz nominal, 119.992 Hz measured  
**Session:** 246.0 s, of which 209.0 s with at least one contact

> The rate figures, and the observations that depend on them, were regenerated on
> 2026-09-22 from this export's raw samples, to give the average gap between reports
> rather than the most common one. Everything else is as exported.

## Headline

| Metric | Value |
| --- | --- |
| Report rate | **53.3 Hz** |
| Rate at 1 contact | 86.5 Hz |
| Rate at 10 contacts | 40.4 Hz |
| Gap between reports | **varies**; see [How the rate is measured](#how-the-rate-is-measured) |
| Mean over every gap | 53.3 Hz |
| Interval jitter (sd) | 4.796 ms |
| Worst gap | 30.00 ms |
| Delivery latency p50 / p99 | 0.31 / 0.60 ms |
| Max simultaneous contacts | 10 |
| Render frame rate | 3548 fps mean |

## Report rate by contact count

How the digitizer's report rate changes as fingers are added. Each row covers
the intervals measured while exactly that many contacts were down. Mean Hz counts
every gap, so it falls below Rate Hz when reports go missing.

| Contacts | Rate Hz | Mean Hz | Interval ms | Jitter sd ms | Worst gap ms | Intervals | % of 1 contact |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 86.5 | 86.5 | 11.561 | 0.884 | 18.01 | 1629 | 100.0% |
| 2 | 69.0 | 69.0 | 14.495 | 1.081 | 19.01 | 1921 | 79.8% |
| 3 | 59.3 | 59.3 | 16.864 | 1.487 | 23.00 | 1183 | 68.6% |
| 4 | 52.0 | 52.0 | 19.236 | 1.642 | 24.00 | 604 | 60.1% |
| 5 | 49.3 | 49.3 | 20.285 | 1.918 | 30.00 | 539 | 57.0% |
| 6 | 46.9 | 46.9 | 21.321 | 2.021 | 28.01 | 567 | 54.2% |
| 7 | 46.3 | 46.3 | 21.578 | 2.455 | 29.00 | 807 | 53.6% |
| 8 | 45.0 | 45.0 | 22.224 | 2.327 | 29.00 | 1750 | 52.0% |
| 9 | 42.1 | 42.1 | 23.742 | 2.648 | 30.00 | 1402 | 48.7% |
| 10 | 40.4 | 40.4 | 24.748 | 1.899 | 29.00 | 735 | 46.7% |

Best **86.5 Hz** at 1 contact, worst **40.4 Hz** at 10 contacts — a **53% drop**.

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

**This panel's gap between reports varies.**
With 2 contacts down, its most common gap is 14.99 ms, which alone would suggest 66.7 Hz,
but its normal gaps average 14.49 ms: 69.0 reports a second.

The `all` row lumps every contact count together. This panel's rate changes as fingers
are added, so that row mixes several rates; read the per-count rows instead.

| Contacts | Rate Hz | Most common gap ms | That gap alone, Hz | Difference |
| ---: | ---: | ---: | ---: | ---: |
| all | **53.3** | 19.99 | 50.0 | +6.5% |
| 1 | **86.5** | 11.99 | 83.4 | +3.7% |
| 2 | **69.0** | 14.99 | 66.7 | +3.4% |
| 3 | **59.3** | 16.99 | 58.9 | +0.8% |
| 4 | **52.0** | 19.99 | 50.0 | +3.9% |
| 5 | **49.3** | 19.99 | 50.0 | -1.4% |
| 6 | **46.9** | 19.99 | 50.0 | -6.2% |
| 7 | **46.3** | 22.01 | 45.4 | +2.0% |
| 8 | **45.0** | 20.99 | 47.6 | -5.5% |
| 9 | **42.1** | 24.99 | 40.0 | +5.3% |
| 10 | **40.4** | 25.99 | 38.5 | +5.0% |

## Report timing

History recovery was **on, so coalesced frames were recovered**.

| Metric | Value |
| --- | --- |
| Input frames | 11141 |
| Samples | 57155 (0 recovered from history) |
| Pointer messages | 11235 |
| Raw HID reports | 11349 |
| Interval mean / sd | 18.7708 / 4.7959 ms |
| Interval min / max | 9.9801 / 29.9999 ms |
| Interval p50 / p90 / p99 / p99.9 | 19.051 / 25.045 / 27.093 / 29.021 ms |

Raw HID reports read straight from the digitizer come to 1.02× the input
frame count. A ratio near 1.0 means the pointer stack is losing nothing.

## Input delivery latency

Time from the hardware timestamp on a touch report to the moment the application
dequeued it. This is OS and driver delivery only — it excludes panel scan-out and
display processing, so it is a floor for end-to-end latency, not a measurement of it.

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| device → app (ms) | 57155 | 0.313 | 0.154 | 0.091 | 4.243 |

Percentiles: p50 0.306, p90 0.514, p99 0.599, p99.9 2.411 ms

## Multi-touch

Maximum simultaneous contacts observed: **10** of 10 the hardware reports

Contact counts reached: 1✓ 2✓ 3✓ 4✓ 5✓ 6✓ 7✓ 8✓ 9✓ 10✓

Touch downs / ups: 331 / 331

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| interval between downs (ms) | 309 | 705.77 | 1766.27 | 14.99 | 21006.65 |
| contact dwell, down→up (ms) | 331 | 3672.12 | 8491.14 | 17.00 | 78501.66 |

## OS position processing

Distance between the processed position Windows reports and the raw position from
the digitizer. A non-zero mean means the OS is smoothing or predicting.

| Metric | n | Mean | SD | Min | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| \|processed − raw\| (px) | 57155 | 0.000 | 0.000 | 0.000 | 0.000 |

Observed himetric range: x 0..21617, y 0..8998 (0.01 mm units).

## Display and rendering

| Property | Value |
| --- | --- |
| Monitor | HG585J32 |
| GDI device | `\\.\DISPLAY2` |
| Mode | 1920 × 1080, 32 bpp, DPI 96 (1.00× scale) |
| Nominal refresh | 120.0000 Hz (exact signal timing) |
| DWM composition | 180.0018 Hz (desktop-wide, not this monitor) |
| Measured vblank | 119.9921 Hz (8.3339 ms period, sd 0.0375 ms, 29507 vblanks) |
| Adapter | NVIDIA GeForce RTX 5090 |
| Swap chain | flip-discard, 3 buffers, max frame latency 1 |
| Present mode | immediate (sync interval 0) + allow-tearing |
| Frames rendered | 871961 |
| Frame rate | 3547.6 fps mean, 2062.7 fps 1% low |
| Frame time | mean 0.282 ms, sd 0.085 ms, p99 0.485 ms |
| Presents dropped | n/a in immediate mode |

## Observations

- Report rate is **53 Hz** (a report every 18.77 ms).
- Its gap between reports varies, so with 2 contacts down the most common gap alone
  would suggest 67 Hz, 3% fewer than it sends.
- Interval jitter is **high**: sd 4.80 ms against a 18.77 ms period.
- Worst gap 30.0 ms stayed within 1.6× the normal period; no report was dropped.
- Report rate falls **53%** as contacts are added, down to 40 Hz at 10
  contacts. Dense multi-finger passages will be sampled that slowly.
- Median delivery latency 0.31 ms, p99 0.60 ms.
- The OS applies no measurable smoothing or prediction to reported positions.

Thresholds above are this tool's reporting conventions, not a standard.

## Touch hardware

### TouchScreen — active

| Property | Value |
| --- | --- |
| VID / PID | `VID_27C6  PID_0529` |
| Version | `0x0107` |
| Manufacturer | WingCool Inc. |
| Type | Touch screen (HID usage page `0x0D`, usage `0x04`) |
| Max contacts (OS) | 15 |
| Max contacts (HID descriptor) | 10 |
| Input report | 52 bytes |
| Digitizer extents | 21691 × 9061 logical units |
| Mapped display | 1920 × 1080 px at (0, 0) |
| Spatial resolution | 11.297 × 8.390 device steps per display pixel |
| Enumerated via | pointer device stack + raw input |
| Interface path | `\\?\HID#VID_27C6&PID_0529&MI_00&Col02#b&1d5316ab&0&0001#{4d1e55b2-f16f-11cf-88cb-001111000030}` |

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

### TouchScreen

| Property | Value |
| --- | --- |
| VID / PID | `VID_27C6  PID_0529` |
| Version | `0x0107` |
| Manufacturer | WingCool Inc. |
| Type | Pen (HID usage page `0x0D`, usage `0x02`) |
| Max contacts (OS) | 1 |
| Max contacts (HID descriptor) | 1 |
| Input report | 8 bytes |
| Digitizer extents | 21691 × 9061 logical units |
| Mapped display | 1920 × 1080 px at (0, 0) |
| Spatial resolution | 11.297 × 8.390 device steps per display pixel |
| Enumerated via | pointer device stack + raw input |
| Interface path | `\\?\HID#VID_27C6&PID_0529&MI_00&Col01#b&1d5316ab&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}` |

### Trackpad / Boot

| Property | Value |
| --- | --- |
| VID / PID | `VID_05AC  PID_0265` |
| Version | `0x0855` |
| Manufacturer | Apple Inc. |
| Type | Touch pad (HID usage page `0x0D`, usage `0x05`) |
| Input report | 17 bytes |
| Enumerated via | raw input |
| Interface path | `\\?\HID#VID_05AC&PID_0265&MI_01&Col02#b&225a8ba9&0&0001#{4d1e55b2-f16f-11cf-88cb-001111000030}` |

System reports touch support: yes. `SM_MAXIMUMTOUCHES` = 15.

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
