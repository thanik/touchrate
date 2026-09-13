# Tested monitor results

**This is not a buyer's guide.** Panels sold under the same name can ship with a
different digitizer from one manufacturing lot to the next, so your unit may
measure differently. Marketplace links may have expired.

Panels measured with [TouchRate](../README.md). Rate is the modal report rate
with one finger down versus ten; latency is the median delivery latency. Full
figures and hardware identification are on each panel's own page.

| Model | Contacts | Rate 1 → 10 fingers | Latency p50 | Comment | Test Result | Link |
| --- | ---: | ---: | ---: | --- | --- | --- |
| EVICIV 18.5" 120Hz Touchscreen Monitor, 1080P for Laptop/PC/Game Console | 10 | 84 → 38 Hz | 0.31 ms | ❌ Bad polling rate, and touch points drop once 5+ fingers are down at the same time | [Result](eviciv-18-5-120hz-1080p/README.md) | [Amazon UK](https://www.amazon.co.uk/dp/B0G4QZD6XX) |
| Waveshare 15.6inch Universal Portable Touch Monitor, 1920×1080 Full HD | 10 | 124 → 124 Hz | 0.65 ms | ✔️ Stable polling rate all the way to 10 fingers, but ~125 Hz looks a bit mid-tier to me — I still saw a touch point drop occasionally with all 10 down at once | [Result](waveshare-15-6-universal-portable-touch-1080p/README.md) | [Waveshare](https://www.waveshare.com/15.6inch-FHD-Monitor.htm) |
| UPERFECT Portable Monitor Touchscreen 120Hz 18.5" | 10 | 59 → 59 Hz | 0.30 ms | ➖ Stable polling rate from 1 to 10 fingers and no touch drops so far, even with 10 fingers, but ~60 Hz is too low | [Result](uperfect-18-5-120hz/README.md) | [Amazon UK](https://www.amazon.co.uk/dp/B0DR8TM4Y5) |
| UPERFECT 15.6" Portable Monitor Touchscreen (non-kickstand model) | 10 | 124 → 124 Hz | 0.20 ms | ✔️ Stable polling rate all the way to 10 fingers. It uses the same ILITEK touch controller as the Waveshare with identical figures, so it is likely the same digitizer — and touch points occasionally drop, just as on the Waveshare | [Result](uperfect-15-6-non-kickstand/README.md) | [Amazon UK](https://www.amazon.co.uk/dp/B0FS1Y3LT3) |

## Adding a result

1. Run TouchRate on the panel in full screen (`F`), so touches at the screen
   edges are measured rather than taken by Windows.
2. Press one finger, then two, and so on up to ten, holding each for several
   seconds so every contact-count row is well supported. Drag around, including
   along the edges, to exercise the tracking.
3. Press `S` to export.
4. Copy `exports/touchrate_<stamp>/` into this folder under a recognisable name,
   for example `brand-name-15-6-portable-monitor/`, and link it from
   **Test Result**.
5. Add a row. Every measured figure is in that folder's `README.md` — the
   headline table and the report-rate-by-contact-count table.
6. Write the comment yourself. Say what the numbers miss: whether it tracks
   accurately near the edges, whether fast swipes stay smooth, whether it feels
   responsive in an actual game.

`exports/` is ignored by git, so only what you copy into this folder is committed.
