# Tested monitor results

**This is not a buyer's guide**
Panels sold under the same name can ship with a different digitizer from one
manufacturing lot to the next, so your unit may measure differently.
Marketplace links may have expired.

Panels measured with [TouchRate](../README.md). Rate is the report rate with
one finger down versus ten; latency is the median delivery latency. Full
figures and hardware identification are on each panel's own page.

Not every panel spaces its reports evenly — the gap can alternate between two
values, say 10 ms and then 11 ms — so the rate here is averaged over the gaps
rather than taken from the most common one, which would name a rate such a
panel never delivers. Each panel's page gives both.

| Model | Contacts | Rate 1 → 10 fingers | Latency p50 | Comment | Test Result | Link |
| --- | ---: | ---: | ---: | --- | --- | --- |
| EVICIV 18.5" 120Hz Touchscreen Monitor, 1080P for Laptop/PC/Game Console | 10 | 87 → 40 Hz | 0.31 ms | ❌ Bad polling rate, and touch points drop once 5+ fingers are down at the same time | [Result](eviciv-18-5-120hz-1080p/README.md) | [Amazon UK](https://www.amazon.co.uk/dp/B0G4QZD6XX) |
| Waveshare 15.6inch Universal Portable Touch Monitor, 1920×1080 Full HD | 10 | 125 → 125 Hz | 0.65 ms | ✔️ Stable polling rate all the way to 10 fingers, but ~125 Hz looks a bit mid-tier to me — I still saw a touch point drop occasionally with all 10 down at once, though fewer fingers seem fine | [Result](waveshare-15-6-universal-portable-touch-1080p/README.md) | [Waveshare](https://www.waveshare.com/15.6inch-FHD-Monitor.htm) |
| UPERFECT Portable Monitor Touchscreen 120Hz 18.5" | 10 | 58 → 58 Hz | 0.30 ms | ➖ Stable polling rate from 1 to 10 fingers and no touch drops so far, even with 10 fingers, but ~60 Hz is too low | [Result](uperfect-18-5-120hz/README.md) | [Amazon UK](https://www.amazon.co.uk/dp/B0DR8TM4Y5) |
| UPERFECT 15.6" Portable Monitor Touchscreen (non-kickstand model) | 10 | 125 → 125 Hz | 0.20 ms | ➖ Stable polling rate all the way to 10 fingers. Seems like it uses the same ILITEK touch controller as the Waveshare with identical USB Product ID — but touch points drop a little more often than on the Waveshare with 2+ fingers down | [Result](uperfect-15-6-non-kickstand/README.md) | [Amazon UK](https://www.amazon.co.uk/dp/B0FS1Y3LT3) |
| UPERFECT 21.5" Portable Monitor Touchscreen 120Hz FHD Screen Aluminum Shell | 10 | 86 → 76 Hz | 0.26 ms | ❌ The screen is large and looks nice, but the polling rate drops once 7+ fingers are down, and even the best rate is a bit low. Touch points merge into one when fingers are close together | [Result](uperfect-21-5-120hz-1080p/README.md) | [Amazon UK](https://www.amazon.co.uk/dp/B0FJL6FJNJ) |
| Magedok 17.3" 1080P FHD 300Hz Touchscreen Monitor (MG300-FT01) | 10 | 95 → 95 Hz | 1.21 ms | ✔️ Stable polling rate from 1 to 10 fingers and no touch point drops so far. Its reports are not evenly spaced: the gap alternates between 10 ms and 11 ms, which averages out to the 95 Hz above — mid-tier, the same bracket as the Waveshare. Counting only the more common gap would call it 100 Hz, which it never delivers. It also sends its contacts five at a time, so ten fingers arrive as two reports. The touch point follows the finger nicely on a high refresh rate display | [Result](magedok-mg300-ft01/README.md) | [Magedok](https://store.magedok.com/products/17-3-inch-300hz-touch-monitor-with-mg300-ft01), [Amazon UK](https://www.amazon.co.uk/dp/B09W286P1V) |

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
5. Add a row. Every measured figure is in that folder's `README.md`: take the
   rate from the peak-centred column at 1 and at 10 contacts in **Report rate,
   peak-centred**, and the latency from the headline table.
6. Write the comment yourself. Say what the numbers miss: whether it tracks
   accurately near the edges, whether fast swipes stay smooth, whether it feels
   responsive in an actual game.

`exports/` is ignored by git, so only what you copy into this folder is committed.
