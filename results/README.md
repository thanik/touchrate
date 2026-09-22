# Tested monitor results

**This is not a buyer's guide**
Panels sold under the same name can ship with a different digitizer from one
manufacturing lot to the next, so your unit may measure differently.
Marketplace links may have expired.

Panels measured with [TouchRate](../README.md). Rate is the report rate with
one finger down versus ten; latency is the median delivery latency. Full
figures and hardware identification are on each panel's own page.

The rate is the average gap between reports, turned into reports per second,
and it is the figure to compare panels by. A panel's page gives the same figure
as **Rate at 1 contact** and **Rate at 10 contacts** in its headline.

| Model | Contacts | Rate 1 → 10 fingers | Latency p50 | Comment | Test Result | Link |
| --- | ---: | ---: | ---: | --- | --- | --- |
| EVICIV 18.5" 120Hz Touchscreen Monitor, 1080P for Laptop/PC/Game Console | 10 | 86.5 → 40.4 Hz | 0.31 ms | ❌ Bad polling rate, and touch points drop once 5+ fingers are down at the same time | [Result](eviciv-18-5-120hz-1080p/README.md) | [Amazon UK](https://www.amazon.co.uk/dp/B0G4QZD6XX) |
| Waveshare 15.6inch Universal Portable Touch Monitor, 1920×1080 Full HD | 10 | 125.0 → 125.0 Hz | 0.65 ms | ✔️ Stable polling rate all the way to 10 fingers, but ~125 Hz looks a bit mid-tier to me — I still saw a touch point drop occasionally with all 10 down at once, though fewer fingers seem fine | [Result](waveshare-15-6-universal-portable-touch-1080p/README.md) | [Waveshare](https://www.waveshare.com/15.6inch-FHD-Monitor.htm) |
| UPERFECT Portable Monitor Touchscreen 120Hz 18.5" | 10 | 58.1 → 58.1 Hz | 0.30 ms | ➖ Stable polling rate from 1 to 10 fingers and no touch drops so far, even with 10 fingers, but ~60 Hz is too low | [Result](uperfect-18-5-120hz/README.md) | [Amazon UK](https://www.amazon.co.uk/dp/B0DR8TM4Y5) |
| UPERFECT 15.6" Portable Monitor Touchscreen (non-kickstand model) | 10 | 125.0 → 125.0 Hz | 0.20 ms | ➖ Stable polling rate all the way to 10 fingers. Seems like it uses the same ILITEK touch controller as the Waveshare with identical USB Product ID — but touch points drop a little more often than on the Waveshare with 2+ fingers down | [Result](uperfect-15-6-non-kickstand/README.md) | [Amazon UK](https://www.amazon.co.uk/dp/B0FS1Y3LT3) |
| UPERFECT 21.5" Portable Monitor Touchscreen 120Hz FHD Screen Aluminum Shell | 10 | 85.6 → 76.2 Hz | 0.26 ms | ❌ The screen is large and looks nice, but the polling rate drops once 7+ fingers are down, and even the best rate is a bit low. Touch points merge into one when fingers are close together | [Result](uperfect-21-5-120hz-1080p/README.md) | [Amazon UK](https://www.amazon.co.uk/dp/B0FJL6FJNJ) |
| Magedok 17.3" 1080P FHD 300Hz Touchscreen Monitor (MG300-FT01) | 10 | 95.4 → 95.5 Hz | 1.17 ms | ✔️ Stable polling rate from 1 to 10 fingers and no touch point drops so far. Its gap between reports is not steady: it switches between about 10 ms and 11 ms, which averages out to about 95 reports a second — mid-tier, the same bracket as the Waveshare. Going by the more common 10 ms gap alone would make it look like a 100 Hz panel. It also sends its contacts five at a time, so ten fingers arrive as two reports. The touch point follows the finger nicely on a high refresh rate display | [Result](magedok-mg300-ft01/README.md) | [Magedok](https://store.magedok.com/products/17-3-inch-300hz-touch-monitor-with-mg300-ft01), [Amazon UK](https://www.amazon.co.uk/dp/B09W286P1V) |

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
5. Add a row. Every figure it needs is in the headline table of that folder's
   `README.md`: the rate is **Rate at 1 contact** and **Rate at 10 contacts**,
   and the latency is the first figure of **Delivery latency p50 / p99**.
6. Write the comment yourself. Say what the numbers miss: whether it tracks
   accurately near the edges, whether fast swipes stay smooth, whether it feels
   responsive in an actual game.

`exports/` is ignored by git, so only what you copy into this folder is committed.
