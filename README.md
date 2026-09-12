# TouchRate

## Background

I was looking for a portable touch monitor suitable for playing rhythm games on
a Windows PC, and found there is very little public data on the polling rate of
these monitors — apart from [the page on iidx.org](https://iidx.org/touch_monitor).
So I measured them myself, and built a tool to do it consistently.

## Tested monitor results

**[→ Skip to measurements for every panel I tested personally so far](results/README.md)**

## Testing/Evaluating Tool

A native Windows tool for evaluating a touch screen for rhythm games and other
latency-sensitive input. It measures the digitizer's report rate and timing
jitter, verifies 10-finger tracking, identifies the touch hardware by VID/PID,
measures the monitor's real refresh rate and the app's own frame rate, and
exports everything for offline analysis.

Direct3D 11 with a flip-model swap chain, immediate present and tearing allowed,
so what you see on the panel is as close to the input as the display path
permits.

![TouchRate](docs/screenshot.png)

## Build

Requires Visual Studio (2019 or newer) with **Desktop development with C++**,
or a standalone Windows SDK + MSVC build tools.

```bat
build.bat
```

Output is `build\TouchRate.exe` — a single self-contained executable, statically
linked against the CRT, with no runtime dependencies beyond Windows itself.

```bat
build.bat debug
build.bat clean
```

## Run

```bat
build\TouchRate.exe
```

| Option | Meaning |
| --- | --- |
| `--list` | Print detected digitizers with VID/PID and exit |
| `--vsync` | Start with vsync on (default is immediate present) |
| `--fullscreen` | Start borderless full screen |
| `--no-history` | Do not recover coalesced pointer frames |
| `--capacity=N` | Sample rows buffered for export (default 500000) |
| `--export-dir=PATH` | Where `[S]` writes files (default `<exe dir>\exports`) |
| `--log` | Start streaming samples to CSV immediately |

### Keys

| Key | Action |
| --- | --- |
| `S` | Export everything measured so far |
| `L` | Start/stop streaming every sample to CSV |
| `O` | Open the export folder |
| `R` | Reset all measurements |
| `C` | Clear the drawn ink and trails |
| `V` | Toggle vsync |
| `H` | Toggle coalesced-frame recovery |
| `T` `G` `I` `P` | Toggle trails / grid / ink / sample dots |
| `D` | Re-enumerate touch devices |
| `F` | Borderless full screen |
| `F1` | Help |
| `Esc` | Quit |

## What it measures

### Touch report rate

Counts distinct **input frames** using the hardware timestamp
(`POINTER_INFO::PerformanceCount`) carried on each pointer report. One frame is
one scan of the digitizer, regardless of how many fingers it contains — so the
rate does not inflate when you add fingers.

Windows coalesces pointer reports when an app cannot keep up. TouchRate replays
the per-frame history (`GetPointerFrameTouchInfoHistory`) to recover every
report the hardware actually sent. Press `H` to turn that off and see the
delivery rate an app would observe if it read only the newest sample per
message — the difference between the two is how much a slow app would lose.

Reported alongside it:

- **modal rate** — the most common interval, i.e. the device's nominal rate
- **mean rate** — 1 / mean interval, which drops if the device stalls
- **jitter (sd)** — the number that matters most for rhythm games; a steady
  100 Hz beats an erratic 200 Hz
- **worst gap** — the longest interval between consecutive reports; a single
  long gap is a dropped note
- **p99 / p99.9** — the tail, where the misses live

Intervals are only measured while the screen is continuously touched. The pause
between two separate touches is the user's, not the digitizer's, and is never
counted as a gap.

### Report rate by contact count

Many panels slow down as fingers are added, which is exactly the case a rhythm
game hits during dense passages. TouchRate attributes every measured interval
to the number of contacts the digitizer was tracking while that interval
elapsed, and reports the rate for each count separately.

The multi-touch panel shows the modal rate under each finger-count pip live,
amber once a count runs below 80% of the best rate observed. The report carries
the full table — modal and mean Hz, mean interval, jitter, worst gap and sample
count per contact count — plus each count as a percentage of the single-contact
baseline, and `raw/rate_by_contacts.csv` has the same data for plotting.

A real example — the [EVICIV 18.5"](results/eviciv-18-5-120hz-1080p/README.md)
panel, whose full export is in this repo:

| contacts | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| modal Hz | 84 | 67 | 59 | 50 | 50 | 50 | 45 | 48 | 40 | 38 |
| % of 1 finger | 100 | 80 | 70 | 60 | 60 | 60 | 54 | 57 | 48 | 46 |

Press one finger, then two, and so on up to ten, holding each for a few seconds
so every row gets enough intervals — the `samples` column tells you which rows
are well supported.

### Raw HID cross-check

TouchRate also reads the digitizer through Raw Input, counting HID reports that
never pass through the pointer stack. If this matches the input-frame count,
nothing is being lost on the way up. If it is higher, the pointer stack is
dropping reports.

### Delivery latency

Time from the hardware timestamp on a report to the moment this process
dequeued it. This covers **driver and OS input handling only**. It excludes
panel scan-out and display processing, so it is a floor for end-to-end latency,
not a measurement of it — true photon latency needs external instrumentation
(a high-speed camera or a photodiode rig).

Note also that a device whose driver stamps reports at OS ingestion rather than
at hardware sampling will report an optimistically low figure here. Compare
against the raw HID count and the interval distribution before trusting it.

### 10-finger test

Tracks up to 20 concurrent contacts with stable slot assignment and colour.
The pips in the multi-touch panel light up as you reach each simultaneous
contact count, so pressing all ten fingers fills the row. The panel compares
what you reached against the **HID descriptor's** contact count, which is the
hardware truth — Windows often advertises a larger figure (`SM_MAXIMUMTOUCHES`)
than the panel can actually report at once.

A watchdog releases any contact that stops reporting for 2 s without an up
message. If that ever triggers, the report says so — it means the input stream
lost a release, which in a game shows up as a stuck note.

### OS position processing

The distance between the processed position Windows reports
(`ptPixelLocation`) and the unprocessed one (`ptPixelLocationRaw`). A non-zero
mean means the OS is smoothing or predicting your input. TouchRate draws and
records the raw position.

### Display and frame rate

- **nominal refresh** — exact rational signal timing from the display config
  (e.g. 119.998 Hz, not a rounded 120)
- **measured refresh** — real vblank intervals timed on a dedicated thread,
  which is the panel's actual rate including any variance
- **render fps / frame time / 1% low** — the app's own frame pacing, so you can
  confirm the tool itself is not the bottleneck

The DWM composition figure is desktop-wide and will differ from the measured
vblank on a multi-monitor setup with mixed refresh rates.

### Spatial resolution

Digitizer logical extents versus the mapped display area, expressed as device
steps per display pixel. Higher means finer positional quantisation.

## Export

`[S]` writes one folder per run, with the readable summary at the top and the
bulk measurements grouped underneath:

```
exports/touchrate_20260912_125034/
├── README.md      rendered summary - GitHub shows it on opening the folder
├── summary.json   the same figures, machine-readable
└── raw/
    ├── samples.csv.gz
    ├── rate_by_contacts.csv
    ├── contacts.csv
    ├── interval_histogram.csv
    ├── latency_histogram.csv
    └── frametime_histogram.csv
```

The summary is Markdown and named `README.md` so that GitHub, GitLab and
similar render it automatically when you open the run folder — a committed
measurement is readable in the browser with no tooling.

| File | Contents |
| --- | --- |
| `README.md` | Headline figures, rate by contact count, timing, latency, multi-touch, hardware identification and observations |
| `summary.json` | Every figure above, machine-readable |
| `raw/samples.csv.gz` | Every buffered sample: device and host timestamps, interval, instantaneous Hz, latency, client/screen/raw/himetric coordinates, prediction delta, pressure, contact area. gzip — around 6x smaller, and read directly by `pandas.read_csv`, R, 7-Zip and `zcat` |
| `raw/rate_by_contacts.csv` | Report rate for each simultaneous contact count, with percent of the single-contact baseline |
| `raw/contacts.csv` | Per-contact summary: sample count, interval stats, path length, down latency |
| `raw/interval_histogram.csv` | Report-interval distribution with Hz equivalents |
| `raw/latency_histogram.csv` | Delivery-latency distribution |
| `raw/frametime_histogram.csv` | Render frame-time distribution |

`[L]` streams samples continuously to `exports/live/` instead, for sessions
longer than the in-memory ring holds.

In `raw/samples.csv.gz`, all contacts belonging to one hardware report share a
`frame_id` and `device_qpc` — group by those to reconstruct frames. The
`from_history` column marks samples recovered from coalesced frames; their
`latency_ms` is not meaningful (they were already queued), so filter on
`from_history=0` for latency analysis.

## Notes and limitations

- Latency figures cover the input stack only, never the display path.
- The persistent ink layer is cleared when the window is resized.
- Immediate present renders uncapped by design, which keeps input-to-present
  latency minimal but keeps the GPU busy. Press `V` for vsync if you would
  rather not.
- The process runs at high priority so that the tool's own scheduling does not
  add jitter to what it is measuring.
- Raw Input is registered with `RIDEV_INPUTSINK`, so HID report counts include
  touches made while another window has focus.
- Touch feedback visuals are disabled for the window; the shell draws those on
  top and adds latency of its own.

## Credits

Inspired by [iidx.org/touch_monitor](https://iidx.org/touch_monitor), which was
the only public collection of touch-monitor polling-rate data I could find when
I started looking, and the reason I went and measured these panels myself.

## AI disclosure

The TouchRate tool's source code was written by Claude (Anthropic's Claude
Code), working from my requirements and iterating against real hardware.

The measurements are not AI-generated. Every result in
[results/](results/README.md) comes from running the tool on a physical panel,
and the comment column in that table is my own assessment from actually using
each monitor.

## License

Public domain, via [the Unlicense](LICENSE). Copy it, modify it, ship it, sell
it, fork it — no attribution required and no conditions attached. TouchRate has
no third-party dependencies, so nothing else's terms apply to the binary you
build either.
