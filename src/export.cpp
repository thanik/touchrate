#include "export.h"
#include "gzip.h"
#include <shlwapi.h>

// ------------------------------------------------------------------ path utils

std::wstring ExeDirectory()
{
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, _countof(buf));
    if (!n) return L".";
    std::wstring p(buf, n);
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

std::wstring DefaultExportDir()
{
    return ExeDirectory() + L"\\exports";
}

bool EnsureDirectory(const std::wstring& path)
{
    if (path.empty()) return false;
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    DWORD e = GetLastError();
    if (e == ERROR_ALREADY_EXISTS) return true;
    if (e != ERROR_PATH_NOT_FOUND) return false;

    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos || slash < 3) return false;
    if (!EnsureDirectory(path.substr(0, slash))) return false;
    return CreateDirectoryW(path.c_str(), nullptr) ||
           GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring TimeStampString()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[32];
    swprintf_s(buf, L"%04u%02u%02u_%02u%02u%02u",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static std::wstring IsoNow()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[64];
    swprintf_s(buf, L"%04u-%02u-%02uT%02u:%02u:%02u",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// --------------------------------------------------------------- file wrapper

namespace {

struct Out
{
    FILE* f = nullptr;
    bool Open(const std::wstring& path)
    {
        if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
        setvbuf(f, nullptr, _IOFBF, 1 << 20);
        return true;
    }
    ~Out() { if (f) fclose(f); }
    void P(const char* fmt, ...)
    {
        va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    }
    void S(const char* s) { fputs(s, f); }
};

std::string Esc(const std::string& s)   // minimal JSON string escape
{
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n";  break;
        case '\r': o += "\\r";  break;
        case '\t': o += "\\t";  break;
        default:
            if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
            else o += c;
        }
    }
    return o;
}

void HistogramCsv(Out& o, const Histogram& h, const char* valueName, bool asHz)
{
    o.P("bin_index,%s_low,%s_high,%s_center,count,fraction", valueName, valueName, valueName);
    if (asHz) o.S(",hz_equivalent");
    o.S("\n");
    double tot = h.total ? (double)h.total : 1.0;
    o.P("underflow,,,,%llu,%.9f", (unsigned long long)h.under, (double)h.under / tot);
    if (asHz) o.S(",");
    o.S("\n");
    for (int i = 0; i < h.nbins; ++i)
    {
        if (!h.bins[(size_t)i]) continue;
        double lo = h.lo + i * h.binW, hi = lo + h.binW, ctr = lo + h.binW * 0.5;
        o.P("%d,%.4f,%.4f,%.4f,%llu,%.9f", i, lo, hi, ctr,
            (unsigned long long)h.bins[(size_t)i], (double)h.bins[(size_t)i] / tot);
        if (asHz) o.P(",%.3f", ctr > 0 ? 1000.0 / ctr : 0.0);
        o.S("\n");
    }
    o.P("overflow,,,,%llu,%.9f", (unsigned long long)h.over, (double)h.over / tot);
    if (asHz) o.S(",");
    o.S("\n");
}

} // namespace

// ---------------------------------------------------------------- samples CSV

// The sample table dominates an export - tens of megabytes for a long session -
// and compresses roughly sixfold, so it is written as gzip. Every analysis tool
// that reads CSV reads .csv.gz directly.
static bool WriteSamples(const std::wstring& path, const Tracker& t, uint64_t& rows,
                         uint64_t& rawBytes, uint64_t& gzBytes)
{
    GzipWriter o;
    if (!o.Open(path)) return false;
    o.S("seq,kind,pointer_id,slot,frame_id,from_history,pointer_type,"
        "device_qpc,device_ms,host_qpc,host_ms,dt_ms,inst_hz,latency_ms,"
        "client_x,client_y,screen_x,screen_y,raw_screen_x,raw_screen_y,"
        "himetric_x,himetric_y,predict_dx,predict_dy,"
        "pressure,contact_w,contact_h,orientation\n");

    const size_t n = t.RecordCount();
    int64_t t0 = n ? t.RecordAt(0).deviceQpc : 0;
    for (size_t i = 0; i < n; ++i)
    {
        const ExportRec& r = t.RecordAt(i);
        double devMs = QpcToMs(r.deviceQpc - t0);
        double hostMs = QpcToMs(r.hostQpc - t0);
        double hz = r.dtMs > 0 ? 1000.0 / r.dtMs : 0.0;
        o.P("%zu,%s,%u,%u,%u,%u,%s,"
            "%lld,%.6f,%lld,%.6f,%.6f,%.3f,%.6f,"
            "%.2f,%.2f,%d,%d,%d,%d,%d,%d,%d,%d,",
            i, SampleKindName(r.kind), r.pointerId, r.slot, r.frameId, r.fromHistory,
            r.pointerType == PT_PEN ? "pen" : "touch",
            (long long)r.deviceQpc, devMs, (long long)r.hostQpc, hostMs, r.dtMs, hz, r.latencyMs,
            r.x, r.y, r.pxX, r.pxY, r.rawX, r.rawY, r.hmX, r.hmY,
            r.pxX - r.rawX, r.pxY - r.rawY);
        if (r.pressure >= 0) o.P("%.4f,", r.pressure); else o.S(",");
        if (r.cw >= 0) o.P("%.1f,%.1f,", r.cw, r.ch); else o.S(",,");
        if (r.orient >= 0) o.P("%.1f\n", r.orient); else o.S("\n");
    }
    rows = n;
    rawBytes = o.RawBytes();
    if (!o.Close()) return false;
    gzBytes = o.CompressedBytes();
    return true;
}

// ------------------------------------------------------------- per-contact CSV

static bool WriteContacts(const std::wstring& path, const Tracker& t)
{
    Out o;
    if (!o.Open(path)) return false;
    if (t.IsPad())
    {
        // Touch pad contacts: positions and path length in pad units.
        o.S("slot,active,contact_id,samples,mean_dt_ms,sd_dt_ms,min_dt_ms,max_dt_ms,"
            "mean_hz,path_len_units,x,y\n");
        for (int i = 0; i < kMaxSlots; ++i)
        {
            const Contact& c = t.Slot(i);
            if (!c.everUsed) continue;
            double hz = c.dt.n && c.dt.mean > 0 ? 1000.0 / c.dt.mean : 0.0;
            o.P("%d,%d,%u,%llu,%.4f,%.4f,%.4f,%.4f,%.3f,%.1f,%.1f,%.1f\n",
                i, c.active ? 1 : 0, c.pointerId, (unsigned long long)c.samples,
                c.dt.n ? c.dt.mean : 0.0, c.dt.Sd(), c.dt.n ? c.dt.mn : 0.0, c.dt.n ? c.dt.mx : 0.0,
                hz, c.pathLenPx, c.x, c.y);
        }
        return true;
    }
    o.S("slot,active,pointer_id,samples,mean_dt_ms,sd_dt_ms,min_dt_ms,max_dt_ms,"
        "mean_hz,path_len_px,down_latency_ms,x,y,pressure,contact_w,contact_h\n");
    for (int i = 0; i < kMaxSlots; ++i)
    {
        const Contact& c = t.Slot(i);
        if (!c.everUsed) continue;
        double hz = c.dt.n && c.dt.mean > 0 ? 1000.0 / c.dt.mean : 0.0;
        o.P("%d,%d,%u,%llu,%.4f,%.4f,%.4f,%.4f,%.3f,%.1f,%.4f,%.1f,%.1f,",
            i, c.active ? 1 : 0, c.pointerId, (unsigned long long)c.samples,
            c.dt.n ? c.dt.mean : 0.0, c.dt.Sd(), c.dt.n ? c.dt.mn : 0.0, c.dt.n ? c.dt.mx : 0.0,
            hz, c.pathLenPx, c.downLatencyMs, c.x, c.y);
        if (c.pressure >= 0) o.P("%.4f,", c.pressure); else o.S(",");
        if (c.cw >= 0) o.P("%.1f,%.1f\n", c.cw, c.ch); else o.S(",\n");
    }
    return true;
}
// A touch pad's samples, in pad units. device_ms is the pad's scan-time clock,
// re-anchored to the host clock at the start of every touch.
static bool WritePadSamples(const std::wstring& path, const Tracker& t, uint64_t& rows)
{
    GzipWriter o;
    if (!o.Open(path)) return false;
    o.S(kPadCsvHeader);
    const size_t n = t.RecordCount();
    const int64_t t0 = n ? t.RecordAt(0).deviceQpc : 0;
    for (size_t i = 0; i < n; ++i)
    {
        const ExportRec& r = t.RecordAt(i);
        const double hz = r.dtMs > 0 ? 1000.0 / r.dtMs : 0.0;
        o.P("%zu,%s,%u,%u,%u,%lld,%.6f,%lld,%.6f,%.6f,%.3f,%.1f,%.1f\n",
            i, SampleKindName(r.kind), r.pointerId, r.slot, r.frameId,
            (long long)r.deviceQpc, QpcToMs(r.deviceQpc - t0), (long long)r.hostQpc,
            QpcToMs(r.hostQpc - t0), r.dtMs, hz, r.x, r.y);
    }
    rows = n;
    return o.Close();
}

static bool WriteRateByCount(const std::wstring& path, const Tracker& tr)
{
    Out o;
    if (!o.Open(path)) return false;
    o.S("contacts,modal_hz,mean_hz,interval_mean_ms,interval_sd_ms,"
        "interval_min_ms,interval_max_ms,max_gap_ms,intervals,"
        "pct_of_single_contact_rate\n");
    const double one = tr.HasDataAt(1) ? tr.ModeHzAt(1) : 0.0;
    for (int n = 1; n <= kMaxSlots; ++n)
    {
        if (!tr.HasDataAt(n)) continue;
        const Stats& s = tr.IntervalMsAt(n);
        o.P("%d,%.4f,%.4f,%.6f,%.6f,%.6f,%.6f,%.6f,%llu,",
            n, tr.ModeHzAt(n), tr.MeanHzAt(n), s.mean, s.Sd(), s.mn, s.mx,
            tr.MaxGapMsAt(n), (unsigned long long)s.n);
        if (one > 0) o.P("%.2f\n", tr.ModeHzAt(n) / one * 100.0);
        else         o.S("\n");
    }
    return true;
}

// ----------------------------------------------------------- Markdown summary

namespace {

// Escape the one character that would break out of a Markdown table cell.
std::string Cell(const std::string& s)
{
    std::string o;
    o.reserve(s.size());
    for (char c : s)
    {
        if (c == '|') o += "\\|";
        else if (c == '\n' || c == '\r') o += ' ';
        else o += c;
    }
    return o;
}

// One "n / mean / sd / min / max" row of a statistics table.
void MdStat(Out& o, const char* label, const Stats& s, int prec = 3)
{
    if (!s.n) { o.P("| %s | - | - | - | - | - |\n", label); return; }
    o.P("| %s | %llu | %.*f | %.*f | %.*f | %.*f |\n",
        label, (unsigned long long)s.n,
        prec, s.mean, prec, s.Sd(), prec, s.mn, prec, s.mx);
}

void MdDevice(Out& o, const TouchDevice& d, const char* tag)
{
    o.P("### %s%s\n\n", Cell(d.Label()).c_str(), tag);
    o.S("| Property | Value |\n| --- | --- |\n");
    o.P("| VID / PID | `%s` |\n", Cell(d.VidPidString()).c_str());
    if (d.version) o.P("| Version | `0x%04X` |\n", d.version);
    if (!d.manufacturer.empty())
        o.P("| Manufacturer | %s |\n", Cell(WideToUtf8(d.manufacturer)).c_str());
    o.P("| Type | %s (HID usage page `0x%02X`, usage `0x%02X`) |\n",
        Cell(WideToUtf8(d.typeName)).c_str(), d.usagePage, d.usage);
    if (d.maxContacts)    o.P("| Max contacts (OS) | %u |\n", d.maxContacts);
    if (d.hidMaxContacts) o.P("| Contact slots per HID report | %u |\n", d.hidMaxContacts);
    if (d.inputReportBytes) o.P("| Input report | %u bytes |\n", d.inputReportBytes);
    if (d.haveRects)
    {
        o.P("| Digitizer extents | %ld × %ld logical units |\n",
            d.deviceRect.right - d.deviceRect.left, d.deviceRect.bottom - d.deviceRect.top);
        o.P("| Mapped display | %ld × %ld px at (%ld, %ld) |\n",
            d.displayRect.right - d.displayRect.left, d.displayRect.bottom - d.displayRect.top,
            d.displayRect.left, d.displayRect.top);
        o.P("| Spatial resolution | %.3f × %.3f device steps per display pixel |\n",
            d.StepsPerPixelX(), d.StepsPerPixelY());
    }
    o.P("| Enumerated via | %s%s |\n",
        d.isPointerDevice ? "pointer device stack" : "",
        d.isRawInputDevice ? (d.isPointerDevice ? " + raw input" : "raw input") : "");
    if (!d.path.empty())
        o.P("| Interface path | `%s` |\n", Cell(WideToUtf8(d.path)).c_str());
    o.S("\n");
}

constexpr double kEdgeBandPx = 50.0;   // "at the edge", stated in the report
constexpr double kGroupMs    = 250.0;  // fingers going down this close together went down at once

// Lost contacts that went down together in threes or more: Windows takes such
// touches as a system gesture. Returns the number of groups.
size_t FingerGroups(const std::vector<UndeliveredTouch>& und, size_t& contacts)
{
    std::vector<int64_t> starts;
    for (const UndeliveredTouch& u : und) starts.push_back(u.qpc);
    std::sort(starts.begin(), starts.end());
    size_t groups = 0;
    contacts = 0;
    for (size_t i = 0; i < starts.size();)
    {
        size_t j = i + 1;
        while (j < starts.size() && QpcToMs(starts[j] - starts[i]) <= kGroupMs) ++j;
        if (j - i >= 3) { ++groups; contacts += j - i; }
        i = j;
    }
    return groups;
}

// Report rate at each contact count, then the drop from best to worst.
void MdRateByCount(Out& o, const Tracker& t)
{
    o.S("| Contacts | Modal Hz | Mean Hz | Interval ms | Jitter sd ms | Worst gap ms | Intervals | % of 1 contact |\n");
    o.S("| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
    const double one = t.HasDataAt(1) ? t.ModeHzAt(1) : 0.0;
    int rows = 0;
    for (int n = 1; n <= kMaxSlots; ++n)
    {
        if (!t.HasDataAt(n)) continue;
        const Stats& s = t.IntervalMsAt(n);
        o.P("| %d | %.1f | %.1f | %.3f | %.3f | %.2f | %llu | ",
            n, t.ModeHzAt(n), t.MeanHzAt(n), s.mean, s.Sd(), t.MaxGapMsAt(n),
            (unsigned long long)s.n);
        if (one > 0) o.P("%.1f%% |\n", t.ModeHzAt(n) / one * 100.0);
        else         o.S("- |\n");
        ++rows;
    }
    if (!rows)
        o.S("| - | - | - | - | - | - | - | - |\n");
    o.S("\n");

    int worstN = 0; double worstHz = 1e9, bestHz = 0; int bestN = 0;
    for (int n = 1; n <= kMaxSlots; ++n)
    {
        if (!t.HasDataAt(n)) continue;
        double hz = t.ModeHzAt(n);
        if (hz < worstHz) { worstHz = hz; worstN = n; }
        if (hz > bestHz) { bestHz = hz; bestN = n; }
    }
    if (worstN && bestN && worstN != bestN)
        o.P("Best **%.1f Hz** at %d contact%s, worst **%.1f Hz** at %d contact%s — a **%.0f%% drop**.\n\n",
            bestHz, bestN, bestN == 1 ? "" : "s",
            worstHz, worstN, worstN == 1 ? "" : "s",
            bestHz > 0 ? (1.0 - worstHz / bestHz) * 100.0 : 0.0);
}

// The timing observations both inputs share. Needs interval data.
void MdRateBullets(Out& o, const Tracker& t)
{
    const Histogram& ih = t.IntervalHist();
    const double mode = t.ModeHz();
    const double modeMs = mode > 0 ? 1000.0 / mode : 0;
    const double sd = t.IntervalMs().Sd();
    const double p999 = ih.Percentile(0.999);

    o.P("- Modal report rate is **%.0f Hz** (%.2f ms per report).\n", mode, modeMs);
    o.P("- Interval jitter is %s: sd %.2f ms against a %.2f ms period.\n",
        sd > modeMs * 0.25 ? "**high**" : "low", sd, modeMs);
    if (modeMs > 0 && p999 > modeMs * 3.0)
        o.P("- Tail stalls present: 1 report in 1000 arrives %.1f ms late or worse.\n", p999);
    if (modeMs > 0 && t.MaxGapMs() > modeMs * 2.5 && t.MaxGapMs() > 25.0)
        o.P("- Worst gap %.1f ms is %.1f× the normal period: at least one report was missed.\n",
            t.MaxGapMs(), t.MaxGapMs() / modeMs);
    else if (modeMs > 0)
        o.P("- Worst gap %.1f ms stayed within %.1f× the normal period; no report was dropped.\n",
            t.MaxGapMs(), t.MaxGapMs() / modeMs);

    int worstN = 0; double worstHz = 1e9, bestHz = 0;
    for (int n = 1; n <= kMaxSlots; ++n)
    {
        if (!t.HasDataAt(n)) continue;
        double hz = t.ModeHzAt(n);
        if (hz < worstHz) { worstHz = hz; worstN = n; }
        if (hz > bestHz) bestHz = hz;
    }
    if (worstN && bestHz > 0)
    {
        double drop = (1.0 - worstHz / bestHz) * 100.0;
        if (drop >= 15.0)
            o.P("- Report rate falls **%.0f%%** as contacts are added, down to %.0f Hz at %d\n"
                "  contacts. Dense multi-finger passages will be sampled that slowly.\n",
                drop, worstHz, worstN);
        else if (drop >= 0.5)
            o.P("- Report rate holds within %.0f%% across the contact counts measured.\n", drop);
        else
            o.S("- Report rate does not change with the number of contacts.\n");
    }
}

bool PadMeasured(const ExportContext& ctx)
{
    return ctx.pad && ctx.padIn && ctx.pad->TotalFrames() > 0;
}

const TouchDevice* PadDevice(const ExportContext& ctx)
{
    if (!ctx.devices) return nullptr;
    if (ctx.padDevice >= 0 && (size_t)ctx.padDevice < ctx.devices->size())
        return &(*ctx.devices)[(size_t)ctx.padDevice];
    for (const TouchDevice& d : *ctx.devices) if (d.usage == 0x05) return &d;
    return nullptr;
}

// The field is in 100 us units, but many pads only advance it in whole
// milliseconds.
std::string PadClockStep(const TouchPadInput& in)
{
    char b[64];
    if (in.ClockStepMs() >= 0.5) snprintf(b, sizeof b, "which counts in %g ms steps", in.ClockStepMs());
    else if (in.ClockStepMs() > 0) snprintf(b, sizeof b, "at 100 µs resolution");
    else snprintf(b, sizeof b, "in 100 µs units");
    return b;
}

bool PadClockCoarse(const TouchPadInput& in) { return in.ScanClock() && in.ClockStepMs() >= 0.5; }

const char* PadTiming(const TouchPadInput& in, char* buf, size_t n)
{
    if (in.ScanRejected())
        snprintf(buf, n, "arrival time — the pad's scan-time clock ran at %.2f× the host clock, so it was not used",
                 in.ScanRatio());
    else if (in.ScanClock() && in.ScanChecked())
        snprintf(buf, n, "the pad's own scan-time clock, %s, within %.1f%% of the host clock",
                 PadClockStep(in).c_str(), std::fabs(in.ScanRatio() - 1.0) * 100.0);
    else if (in.ScanClock())
        snprintf(buf, n, "the pad's own scan-time clock, %s; too little touching to check it against the host clock",
                 PadClockStep(in).c_str());
    else
        snprintf(buf, n, "arrival time — the pad reports no scan time");
    return buf;
}

// Everything measured from the touch pad, kept apart from the touch screen.
void MdPad(Out& o, const ExportContext& ctx)
{
    const Tracker& p = *ctx.pad;
    const TouchPadInput& in = *ctx.padIn;
    const HidDescriptorInfo& d = in.Desc();
    const Histogram& ih = p.IntervalHist();
    const TouchDevice* dev = PadDevice(ctx);

    o.S("## Touch pad\n\n");
    o.S("Windows never passes a touch pad to applications as touch input: it turns it\n"
        "into cursor movement and gestures. So the pad is measured from its own HID reports,\n"
        "read through Raw Input, and none of these figures are mixed into the touch\n"
        "screen's. Positions are in the pad's own units, not screen pixels.\n\n");

    o.S("| Metric | Value |\n| --- | --- |\n");
    if (dev)
        o.P("| Device | %s — `%s` |\n", Cell(dev->Label()).c_str(), Cell(dev->VidPidString()).c_str());
    if (d.valid)
    {
        o.P("| Surface | %d × %d units", d.xMax - d.xMin, d.yMax - d.yMin);
        if (d.widthMm > 0 && d.heightMm > 0) o.P(", %.1f × %.1f mm", d.widthMm, d.heightMm);
        o.S(" |\n");
    }
    if (PadClockCoarse(in))
        o.P("| Modal report rate | **%.1f Hz** — the centre of the interval peak, since the pad's clock counts in %g ms steps |\n",
            p.ModeHz(), in.ClockStepMs());
    else
        o.P("| Modal report rate | **%.1f Hz** |\n", p.ModeHz());
    o.P("| Mean report rate | %.1f Hz |\n", p.AvgHz());
    if (p.HasDataAt(1)) o.P("| Rate at 1 contact | %.1f Hz |\n", p.ModeHzAt(1));
    if (p.MaxSimultaneous() > 1 && p.HasDataAt(p.MaxSimultaneous()))
        o.P("| Rate at %d contacts | %.1f Hz |\n", p.MaxSimultaneous(), p.ModeHzAt(p.MaxSimultaneous()));
    if (p.IntervalMs().n)
    {
        o.P("| Interval jitter (sd) | %.3f ms |\n", p.IntervalMs().Sd());
        o.P("| Worst gap | %.2f ms |\n", p.MaxGapMs());
    }
    o.P("| Max simultaneous contacts | %d |\n", p.MaxSimultaneous());
    char tb[192];
    o.P("| Timing | %s |\n", PadTiming(in, tb, sizeof tb));
    if (in.ScanClock() && in.AddedJitterMs().n > 1)
        o.P("| Jitter added between pad and app | sd %.3f ms (arrival step minus the pad's own step) |\n",
            in.AddedJitterMs().Sd());
    if (in.ArrivalSteps())
    {
        o.P("| Reports arriving bunched | %.1f%% (%llu of %llu) — held up, then handed over together with the next one |\n",
            100.0 * (double)in.Bunched() / (double)in.ArrivalSteps(),
            (unsigned long long)in.Bunched(), (unsigned long long)in.ArrivalSteps());
        o.P("| Longest wait between arrivals | %.1f ms |\n", in.ArrivalIntervalMs().mx);
    }
    o.P("| HID reports / frames | %llu / %llu |\n",
        (unsigned long long)in.Reports(), (unsigned long long)p.TotalFrames());
    if (in.Hybrid())
        o.P("| Report layout | %u contact slots per report; frames with more contacts are split across reports (hybrid mode), up to %u declared |\n",
            d.fingerCollections, in.MaxDeclared());
    else
        o.P("| Report layout | %u contact slots per report |\n", d.fingerCollections);
    o.P("| Palm-rejected contacts | %llu |\n", (unsigned long long)in.Palms());
    if (d.hasButton) o.P("| Button clicks | %llu |\n", (unsigned long long)in.Clicks());
    o.S("\n");

    o.S("### Touch pad report rate by contact count\n\n");
    MdRateByCount(o, p);
    if (PadClockCoarse(in))
        o.P("The pad's clock counts in %g ms steps, so a single interval reads as a whole step:\n"
            "a steady %.2f ms period shows up as a mix of the steps either side of it. Each modal\n"
            "rate above is the centre of that peak rather than its tallest step, and jitter and\n"
            "worst gap include up to one step of rounding.\n\n",
            in.ClockStepMs(), p.ModeHz() > 0 ? 1000.0 / p.ModeHz() : 0.0);

    o.S("### Touch pad report timing\n\n");
    o.S("| Metric | Value |\n| --- | --- |\n");
    o.P("| Samples | %llu |\n", (unsigned long long)p.TotalSamples());
    if (p.IntervalMs().n)
    {
        const Stats& s = p.IntervalMs();
        o.P("| Interval mean / sd | %.4f / %.4f ms |\n", s.mean, s.Sd());
        o.P("| Interval min / max | %.4f / %.4f ms |\n", s.mn, s.mx);
        o.P("| Interval p50 / p90 / p99 / p99.9 | %.3f / %.3f / %.3f / %.3f ms |\n",
            ih.Percentile(0.50), ih.Percentile(0.90), ih.Percentile(0.99), ih.Percentile(0.999));
    }
    if (in.ArrivalIntervalMs().n)
        o.P("| Arrival interval mean / sd (host clock) | %.4f / %.4f ms |\n",
            in.ArrivalIntervalMs().mean, in.ArrivalIntervalMs().Sd());
    o.P("| Touch downs / ups | %llu / %llu |\n", (unsigned long long)p.Downs(), (unsigned long long)p.Ups());
    o.S("\n");
    o.S("| Metric | n | Mean | SD | Min | Max |\n| --- | ---: | ---: | ---: | ---: | ---: |\n");
    MdStat(o, "interval between downs (ms)", p.TapIntervalMs(), 2);
    MdStat(o, "contact dwell, down→up (ms)", p.DwellMs(), 2);
    o.S("\n");
}

void MdPadObservations(Out& o, const ExportContext& ctx)
{
    const Tracker& p = *ctx.pad;
    const TouchPadInput& in = *ctx.padIn;
    o.S("**Touch pad**\n\n");
    if (!p.IntervalMs().n)
    {
        o.S("- No touch pad report timing was captured. Keep a finger moving on the pad for a\n"
            "  few seconds.\n\n");
        return;
    }
    MdRateBullets(o, p);
    if (in.ScanRejected())
        o.P("- The pad's scan-time clock ran at %.2f× the host clock, not in the 100 µs units\n"
            "  precision touch pads report, so intervals were timed on arrival instead. That\n"
            "  includes the jitter of the pad's connection and of Windows.\n", in.ScanRatio());
    else if (in.ScanClock() && in.AddedJitterMs().n > 1)
        o.P("- Between the pad and the app, %.2f ms of jitter (sd) is added to the pad's own timing.\n",
            in.AddedJitterMs().Sd());
    if (PadClockCoarse(in))
        o.P("- The pad's clock counts in %g ms steps, so single intervals, the jitter and the worst\n"
            "  gap carry up to %g ms of rounding; the modal rate is the centre of the interval peak.\n",
            in.ClockStepMs(), in.ClockStepMs());
    if (in.ArrivalSteps())
    {
        const double pct = 100.0 * (double)in.Bunched() / (double)in.ArrivalSteps();
        if (pct >= 1.0)
            o.P("- %.1f%% of reports reached the app bunched with the next one: held up for about\n"
                "  %.0f ms, then two at once, although the pad produced them %.1f ms apart. Anything\n"
                "  reading the pad sees those reports together. The longest wait was %.0f ms.\n",
                pct, in.BunchWaitMs().n ? in.BunchWaitMs().mean : 0.0,
                p.ModeHz() > 0 ? 1000.0 / p.ModeHz() : 0.0, in.ArrivalIntervalMs().mx);
    }
    if (in.Palms())
        o.P("- The pad classed %llu contact%s as a palm or other unintended touch and left %s out.\n",
            (unsigned long long)in.Palms(), in.Palms() == 1 ? "" : "s", in.Palms() == 1 ? "it" : "them");
    const uint32_t slots = in.Desc().fingerCollections;
    if (!in.Hybrid() && slots > 1 && (uint32_t)p.MaxSimultaneous() < slots)
        o.P("- Only %d simultaneous finger%s reached; the pad reports up to %u. Put %u fingers\n"
            "  down to confirm the full contact count.\n",
            p.MaxSimultaneous(), p.MaxSimultaneous() == 1 ? " was" : "s were", slots, slots);
    o.S("\n");
}

// What the panel reported against what Windows delivered, and why they differ.
void MdDelivery(Out& o, const ExportContext& ctx)
{
    const Tracker& t = *ctx.tracker;
    o.S("## Touch delivery\n\n");
    o.S("Contacts the panel itself reported, decoded from its raw HID reports, compared\n"
        "with the pointer contacts Windows delivered to the app. A contact that appears\n"
        "in the first but not the second never reached the application at all.\n\n");

    // Describe the device that reported, or failing that the active digitizer,
    // so what the panel can express is on record even without touch data.
    HidDescriptorInfo di;
    bool haveDesc = false;
    if (ctx.hid)
    {
        if (ctx.hid->LastScreenDevice()) haveDesc = ctx.hid->Describe(ctx.hid->LastScreenDevice(), di);
        if (!haveDesc && ctx.devices && !ctx.devices->empty())
        {
            const size_t idx = ctx.activeDevice >= 0 ? (size_t)ctx.activeDevice : 0;
            // Never the touch pad: this section is about the touch screen.
            if (idx < ctx.devices->size() && (*ctx.devices)[idx].rawHandle && (*ctx.devices)[idx].usage != 0x05)
                haveDesc = ctx.hid->Describe((*ctx.devices)[idx].rawHandle, di);
        }
    }
    const WindowInfo& w = ctx.window;

    if (!t.HidSeen())
    {
        o.S("No decodable HID touch reports were received, so delivery could not be checked.\n\n");
        if (haveDesc)
            o.P("The digitizer's descriptor declares %u contact slots, TipSwitch %s, Confidence %s,\n"
                "X %d..%d and Y %d..%d.\n\n",
                di.fingerCollections, di.hasTipSwitch ? "yes" : "no", di.hasConfidence ? "yes" : "no",
                di.xMin, di.xMax, di.yMin, di.yMax);
        return;
    }

    o.S("| Metric | Value |\n| --- | --- |\n");
    o.P("| Panel contacts (TipSwitch set) | %llu |\n", (unsigned long long)t.HidContacts());
    o.P("| Delivered to the app | %llu |\n", (unsigned long long)t.HidDelivered());
    o.P("| **Not delivered** | **%llu** |\n", (unsigned long long)t.HidUndelivered());
    o.P("| HID reports: touching / not touching / empty | %llu / %llu / %llu |\n",
        (unsigned long long)t.HidTouchReports(), (unsigned long long)t.HidNoTipReports(),
        (unsigned long long)t.HidEmptyReports());
    o.P("| HID frames / pointer input frames | %llu / %llu |\n",
        (unsigned long long)t.HidFrames(), (unsigned long long)t.TotalFrames());
    o.P("| Window at export | %ld × %ld at (%ld, %ld), %s |\n",
        w.client.right - w.client.left, w.client.bottom - w.client.top,
        w.client.left, w.client.top, w.fullscreen ? "full screen" : "windowed");
    o.P("| Edge-swipe blocking at export | %s |\n",
        w.edgeSwipeBlocked ? "in effect" : "not in effect: the window was not full screen");
    if (t.HidMatchOffsetPx().n)
        o.P("| Position mapping check | %.1f px mean offset across %llu matched samples |\n",
            t.HidMatchOffsetPx().mean, (unsigned long long)t.HidMatchOffsetPx().n);
    if (haveDesc)
        o.P("| HID descriptor | %u contact slots; TipSwitch %s; Confidence %s; X %d..%d, Y %d..%d |\n",
            di.fingerCollections, di.hasTipSwitch ? "yes" : "no", di.hasConfidence ? "yes" : "no",
            di.xMin, di.xMax, di.yMin, di.yMax);
    o.S("\n");

    const std::vector<UndeliveredTouch>& und = t.Undelivered();
    if (!und.empty())
    {
        const size_t shown = std::min<size_t>(und.size(), 25);
        o.S("| # | At | Start x, y | End x, y | Duration | Reports | From edge | Edge swipe blocked |\n");
        o.S("| ---: | ---: | --- | --- | ---: | ---: | ---: | --- |\n");
        for (size_t i = 0; i < shown; ++i)
        {
            const UndeliveredTouch& u = und[i];
            char edge[32] = "-";
            if (u.mapped) snprintf(edge, sizeof edge, "%.0f px", u.edgeDistPx);
            o.P("| %zu | %.2f s | %.0f, %.0f | %.0f, %.0f | %.0f ms | %u | %s | %s |\n",
                i + 1, QpcToSec(u.qpc - ctx.sessionStartQpc), u.x, u.y, u.lastX, u.lastY,
                u.durationMs, u.reports, edge, u.edgeSwipeBlocked ? "yes" : "no");
        }
        o.S("\n");
        if (und.size() > shown)
            o.P("%zu more in [`raw/undelivered_touches.csv`](raw/undelivered_touches.csv).\n\n",
                und.size() - shown);
    }

    o.S("### Diagnosis\n\n");
    const uint64_t lost = t.HidUndelivered();

    if (t.HidContacts() == 0)
    {
        if (t.HidNoTipReports() + t.HidEmptyReports() > 0 && t.TotalFrames() == 0)
            o.P("The panel sent %llu reports but never flagged a contact as touching. Whatever it\n"
                "detected, it did not report as a touch, which points at the panel's own firmware\n"
                "(edge or palm rejection) rather than at Windows.\n\n",
                (unsigned long long)(t.HidNoTipReports() + t.HidEmptyReports()));
        else
            o.S("No panel contacts were recorded in this session.\n\n");
        return;
    }

    if (lost == 0)
    {
        o.P("Every one of the %llu contacts the panel reported was delivered to the app.\n\n",
            (unsigned long long)t.HidContacts());
        return;
    }

    size_t mapped = 0, nearEdge = 0, withoutBlock = 0;
    for (const UndeliveredTouch& u : und)
    {
        if (!u.edgeSwipeBlocked) ++withoutBlock;
        if (!u.mapped) continue;
        ++mapped;
        if (u.edgeDistPx <= kEdgeBandPx) ++nearEdge;
    }

    o.P("**%llu contact%s the panel reported as touching never reached the app.** The\n"
        "panel detected %s and flagged %s as touching, so the loss happened in Windows,\n"
        "not in the panel.\n\n",
        (unsigned long long)lost, lost == 1 ? "" : "s",
        lost == 1 ? "it" : "them", lost == 1 ? "it" : "them");

    if (mapped)
    {
        o.P("%zu of %zu started within %.0f px of the display edge", nearEdge, mapped, kEdgeBandPx);
        if (t.UndeliveredEdgeDist().n && t.DeliveredEdgeDist().n)
            o.P(" (closest approach to the edge averaged %.0f px, against %.0f px for delivered contacts)",
                t.UndeliveredEdgeDist().mean, t.DeliveredEdgeDist().mean);
        o.S(".\n\n");
    }

    if (mapped && nearEdge * 2 >= mapped)
    {
        if (withoutBlock > 0)
            o.P("%zu of them happened while the window was not full screen, so edge-swipe blocking\n"
                "was not in effect. Windows claims touches that start at the screen edge for its\n"
                "swipe gestures and never passes them to the window under the finger, so that is the\n"
                "expected cause. Retest in full screen (`F`), where TouchRate blocks edge swipes.\n\n",
                withoutBlock);
        else
            o.S("All of them happened with edge-swipe blocking in effect, so Windows' edge gestures\n"
                "are not the cause. Check for a window sitting above TouchRate along that edge -\n"
                "the taskbar is the usual one - or a system setting that reserves the edge.\n\n");
    }
    else if (mapped)
    {
        o.S("They are not concentrated at the screen edges, so edge gestures do not explain them.\n\n");
    }

    size_t grouped = 0;
    const size_t groups = FingerGroups(und, grouped);
    if (groups)
        o.P("%zu of them went down in %zu group%s of three or more fingers at once. Windows 11 takes\n"
            "a three- or four-finger touch as a system gesture - switching apps, Task View, showing\n"
            "the desktop - and passes none of it to the window under the fingers. To measure\n"
            "multi-finger touches, turn off **Settings > Bluetooth & devices > Touch > Three- and\n"
            "four-finger touch gestures**.\n\n",
            grouped, groups, groups == 1 ? "" : "s");

    if (t.HidMatchOffsetPx().n && t.HidMatchOffsetPx().mean > 24.0)
        o.P("Note: delivered contacts sat %.0f px from their HID-reported positions on average,\n"
            "so the positions above may be imprecise.\n\n", t.HidMatchOffsetPx().mean);
}

} // namespace

// ------------------------------------------------------------- grid scan

static bool GridScanRan(const ExportContext& ctx)
{
    return ctx.grid && ctx.grid->Samples() > 0 && ctx.grid->Cells() > 0;
}

static void MdGridObservation(Out& o, const GridScan& g)
{
    const int untouched = g.Cells() - g.Touched();
    if (untouched == 0)
        o.P("- Grid scan: all %d cells registered a touch, so there is no dead zone at\n"
            "  this cell size.\n", g.Cells());
    else
        o.P("- Grid scan: %d of %d cells (%.1f%%) registered a touch; %d never did.\n"
            "  See the map for where.\n",
            g.Touched(), g.Cells(), g.Coverage() * 100.0, untouched);
}

static void MdGridScan(Out& o, const GridScan& g)
{
    const RECT& a = g.Area();
    const int w = a.right - a.left, h = a.bottom - a.top;
    GridScan::Side left, top, right, bottom;
    g.EdgeSummary(left, top, right, bottom);
    const int untouched = g.Cells() - g.Touched();

    o.S("## Grid scan (dead zone test)\n\n");
    o.S("The screen was divided into cells and every delivered touch sample marked the cell\n"
        "it landed in. Only reported positions count — a swipe is not filled in between its\n"
        "samples — so a cell left untouched after the whole screen was swept never\n"
        "registered a touch. That is either a dead zone or a spot the sweep missed; going\n"
        "over it again tells the two apart.\n\n");

    o.S("| Metric | Value |\n| --- | --- |\n");
    o.P("| Cells | %d × %d = %d, about %d × %d px each |\n", g.Cols(), g.Rows(), g.Cells(),
        (int)std::lround((double)w / g.Cols()), (int)std::lround((double)h / g.Rows()));
    o.P("| Touched | **%d of %d (%.1f%%)** |\n", g.Touched(), g.Cells(), g.Coverage() * 100.0);
    o.P("| Never touched | %d |\n", untouched);
    o.P("| Samples binned | %llu |\n", (unsigned long long)g.Samples());
    o.P("| Untouched edge cells | left %d/%d, top %d/%d, right %d/%d, bottom %d/%d |\n",
        left.untouched, left.cells, top.untouched, top.cells,
        right.untouched, right.cells, bottom.untouched, bottom.cells);
    o.S("\n");

    // A character map renders wherever the Markdown does, including Git hosts.
    o.S("Map, top of the block at the top of the screen: `\xE2\x96\x88` touched, "
        "`\xC2\xB7` never touched.\n\n```\n");
    for (int r = 0; r < g.Rows(); ++r)
    {
        for (int c = 0; c < g.Cols(); ++c)
            o.S(g.Hits(c, r) ? "\xE2\x96\x88" : "\xC2\xB7");
        o.S("\n");
    }
    o.S("```\n\n");
    if (g.Truncated())
        o.S("More samples arrived than are kept for rebinning. The map is complete unless the\n"
            "cell size was changed late in the scan, which would have left out the samples\n"
            "beyond that limit.\n\n");
    o.S("Per-cell counts are in [`raw/grid_coverage.csv`](raw/grid_coverage.csv).\n\n");
}

// The touch screen's sections: rate, timing, delivery, latency, contacts.
static void MdScreen(Out& o, const ExportContext& ctx, const TouchDevice* dev, bool padToo)
{
    const Tracker& t = *ctx.tracker;
    const Histogram& ih = t.IntervalHist();
    const Histogram& lh = t.LatencyHist();

    // ---- headline
    o.S("## Headline\n\n");
    if (padToo)
        o.S("These are the touch screen's figures. The touch pad was measured separately; see\n"
            "[Touch pad](#touch-pad).\n\n");
    o.S("| Metric | Value |\n| --- | --- |\n");
    o.P("| Modal report rate | **%.1f Hz** |\n", t.ModeHz());
    o.P("| Mean report rate | %.1f Hz |\n", t.AvgHz());
    if (t.HasDataAt(1)) o.P("| Rate at 1 contact | %.1f Hz |\n", t.ModeHzAt(1));
    if (t.MaxSimultaneous() > 1 && t.HasDataAt(t.MaxSimultaneous()))
        o.P("| Rate at %d contacts | %.1f Hz |\n",
            t.MaxSimultaneous(), t.ModeHzAt(t.MaxSimultaneous()));
    if (t.IntervalMs().n)
    {
        o.P("| Interval jitter (sd) | %.3f ms |\n", t.IntervalMs().Sd());
        o.P("| Worst gap | %.2f ms |\n", t.MaxGapMs());
    }
    if (lh.total)
        o.P("| Delivery latency p50 / p99 | %.2f / %.2f ms |\n",
            lh.Percentile(0.50), lh.Percentile(0.99));
    o.P("| Max simultaneous contacts | %d |\n", t.MaxSimultaneous());
    if (ctx.frame)
        o.P("| Render frame rate | %.0f fps mean |\n",
            ctx.frame->AvgMs() > 0 ? 1000.0 / ctx.frame->AvgMs() : 0.0);
    o.S("\n");

    // ---- rate by contact count, the headline result for rhythm games
    o.S("## Report rate by contact count\n\n");
    o.S("How the digitizer's report rate changes as fingers are added. Each row covers\n"
        "the intervals measured while exactly that many contacts were down.\n\n");
    MdRateByCount(o, t);

    // ---- report timing
    o.S("## Report timing\n\n");
    o.P("History recovery was **%s**.\n\n",
        t.UseHistory() ? "on, so coalesced frames were recovered"
                       : "off, so this is the delivered message rate");
    o.S("| Metric | Value |\n| --- | --- |\n");
    o.P("| Input frames | %llu |\n", (unsigned long long)t.TotalFrames());
    o.P("| Samples | %llu (%llu recovered from history) |\n",
        (unsigned long long)t.TotalSamples(), (unsigned long long)t.HistorySamples());
    o.P("| Pointer messages | %llu |\n", (unsigned long long)t.Messages());
    if (t.HidSeen())
        o.P("| Raw HID reports | %llu |\n", (unsigned long long)t.HidReports());
    else
        o.S("| Raw HID reports | none observed |\n");
    if (t.IntervalMs().n)
    {
        const Stats& s = t.IntervalMs();
        o.P("| Interval mean / sd | %.4f / %.4f ms |\n", s.mean, s.Sd());
        o.P("| Interval min / max | %.4f / %.4f ms |\n", s.mn, s.mx);
        o.P("| Interval p50 / p90 / p99 / p99.9 | %.3f / %.3f / %.3f / %.3f ms |\n",
            ih.Percentile(0.50), ih.Percentile(0.90), ih.Percentile(0.99), ih.Percentile(0.999));
    }
    o.S("\n");
    MdDelivery(o, ctx);
    if (GridScanRan(ctx)) MdGridScan(o, *ctx.grid);

    // ---- latency
    o.S("## Input delivery latency\n\n");
    o.S("Time from the hardware timestamp on a touch report to the moment the application\n"
        "dequeued it. This is OS and driver delivery only — it excludes panel scan-out and\n"
        "display processing, so it is a floor for end-to-end latency, not a measurement of it.\n\n");
    o.S("| Metric | n | Mean | SD | Min | Max |\n| --- | ---: | ---: | ---: | ---: | ---: |\n");
    MdStat(o, "device → app (ms)", t.LatencyMs());
    o.S("\n");
    if (lh.total)
        o.P("Percentiles: p50 %.3f, p90 %.3f, p99 %.3f, p99.9 %.3f ms\n\n",
            lh.Percentile(0.50), lh.Percentile(0.90), lh.Percentile(0.99), lh.Percentile(0.999));

    // ---- multi touch
    o.S("## Multi-touch\n\n");
    o.P("Maximum simultaneous contacts observed: **%d**", t.MaxSimultaneous());
    if (dev)
    {
        const unsigned cap = dev->ContactCapacity();
        if (cap) o.P(" of %u the hardware reports", cap);
    }
    o.S("\n\n");
    o.S("Contact counts reached: ");
    for (int i = 1; i <= 10; ++i)
        o.P("%s%d%s", i > 1 ? " " : "", i, t.SawSimultaneous(i) ? "\xE2\x9C\x93" : "\xC2\xB7");
    o.S("\n\n");
    for (int i = 11; i <= kMaxSlots; ++i)
        if (t.SawSimultaneous(i)) o.P("Also reached %d simultaneous contacts.\n\n", i);
    o.P("Touch downs / ups: %llu / %llu\n\n",
        (unsigned long long)t.Downs(), (unsigned long long)t.Ups());
    if (t.LostContacts())
        o.P("> **%llu contact%s released by the watchdog** — no up message arrived within 2 s,\n"
            "> meaning the input stream lost a release.\n\n",
            (unsigned long long)t.LostContacts(), t.LostContacts() == 1 ? " was" : "s were");

    o.S("| Metric | n | Mean | SD | Min | Max |\n| --- | ---: | ---: | ---: | ---: | ---: |\n");
    MdStat(o, "interval between downs (ms)", t.TapIntervalMs(), 2);
    MdStat(o, "contact dwell, down→up (ms)", t.DwellMs(), 2);
    o.S("\n");

    // ---- position processing
    o.S("## OS position processing\n\n");
    o.S("Distance between the processed position Windows reports and the raw position from\n"
        "the digitizer. A non-zero mean means the OS is smoothing or predicting.\n\n");
    o.S("| Metric | n | Mean | SD | Min | Max |\n| --- | ---: | ---: | ---: | ---: | ---: |\n");
    MdStat(o, "\\|processed − raw\\| (px)", t.PredictPx());
    o.S("\n");
    if (t.HimetricSeen())
    {
        int32_t x0, y0, x1, y1;
        t.HimetricRange(x0, y0, x1, y1);
        o.P("Observed himetric range: x %d..%d, y %d..%d (0.01 mm units).\n\n", x0, x1, y0, y1);
    }
}

static bool WriteMarkdown(const std::wstring& path, const ExportContext& ctx)
{
    Out o;
    if (!o.Open(path)) return false;

    const Tracker& t = *ctx.tracker;
    const Histogram& lh = t.LatencyHist();
    const double sessionSec = QpcToSec(ctx.nowQpc - ctx.sessionStartQpc);

    // The touch screen - never the touch pad, which has its own line.
    const TouchDevice* dev = nullptr;
    if (ctx.devices && ctx.activeDevice >= 0 && (size_t)ctx.activeDevice < ctx.devices->size())
        dev = &(*ctx.devices)[(size_t)ctx.activeDevice];
    else if (ctx.devices)
        for (const TouchDevice& d : *ctx.devices) if (d.usage != 0x05) { dev = &d; break; }
    const TouchDevice* padDev = PadDevice(ctx);

    // ---- title
    o.P("# TouchRate measurement — %s\n\n", WideToUtf8(IsoNow()).c_str());

    if (dev)
        o.P("**Touch screen:** %s — `%s`%s%s  \n",
            Cell(dev->Label()).c_str(), Cell(dev->VidPidString()).c_str(),
            dev->manufacturer.empty() ? "" : " — ",
            dev->manufacturer.empty() ? "" : Cell(WideToUtf8(dev->manufacturer)).c_str());
    if (padDev && (PadMeasured(ctx) || !dev))
        o.P("**Touch pad:** %s — `%s`%s%s  \n",
            Cell(padDev->Label()).c_str(), Cell(padDev->VidPidString()).c_str(),
            padDev->manufacturer.empty() ? "" : " — ",
            padDev->manufacturer.empty() ? "" : Cell(WideToUtf8(padDev->manufacturer)).c_str());
    if (ctx.monitor)
    {
        const MonitorInfo& m = *ctx.monitor;
        o.P("**Display:** %s — %d × %d @ %.3f Hz nominal",
            Cell(m.friendlyName.empty() ? WideToUtf8(m.gdiName) : WideToUtf8(m.friendlyName)).c_str(),
            m.width, m.height, m.nominalHz);
        if (ctx.vblank && ctx.vblank->Valid()) o.P(", %.3f Hz measured", ctx.vblank->Hz());
        o.S("  \n");
    }
    if (!PadMeasured(ctx))
        o.P("**Session:** %.1f s, of which %.1f s with at least one contact\n\n",
            sessionSec, t.TouchingSec());
    else if (t.TotalFrames())
        o.P("**Session:** %.1f s, of which %.1f s touching the screen and %.1f s touching the touch pad\n\n",
            sessionSec, t.TouchingSec(), ctx.pad->TouchingSec());
    else
        o.P("**Session:** %.1f s, of which %.1f s with at least one finger on the touch pad\n\n",
            sessionSec, ctx.pad->TouchingSec());

    const bool padData = PadMeasured(ctx);
    const bool screenData = t.TotalFrames() > 0 || t.HidSeen();
    if (screenData || !padData)
        MdScreen(o, ctx, dev, padData);
    if (padData)
        MdPad(o, ctx);

    // ---- display and rendering
    o.S("## Display and rendering\n\n| Property | Value |\n| --- | --- |\n");
    if (ctx.monitor)
    {
        const MonitorInfo& m = *ctx.monitor;
        o.P("| Monitor | %s |\n",
            Cell(m.friendlyName.empty() ? WideToUtf8(m.gdiName) : WideToUtf8(m.friendlyName)).c_str());
        o.P("| GDI device | `%s` |\n", Cell(WideToUtf8(m.gdiName)).c_str());
        o.P("| Mode | %d × %d, %d bpp, DPI %u (%.2f× scale) |\n",
            m.width, m.height, m.bpp, m.dpi, m.dpi / 96.0);
        o.P("| Nominal refresh | %.4f Hz %s |\n", m.nominalHz,
            m.nominalExact ? "(exact signal timing)" : "(rounded mode value)");
        if (m.dwmValid)
            o.P("| DWM composition | %.4f Hz (desktop-wide, not this monitor) |\n", m.dwmHz);
    }
    if (ctx.vblank && ctx.vblank->Valid())
        o.P("| Measured vblank | %.4f Hz (%.4f ms period, sd %.4f ms, %llu vblanks) |\n",
            ctx.vblank->Hz(), ctx.vblank->PeriodMs(), ctx.vblank->JitterMs(),
            (unsigned long long)ctx.vblank->Count());
    o.P("| Adapter | %s |\n", Cell(ctx.present.adapter).c_str());
    o.P("| Swap chain | flip-discard, %u buffers, max frame latency %u |\n",
        ctx.present.bufferCount, ctx.present.maxFrameLatency);
    o.P("| Present mode | %s%s |\n",
        ctx.present.vsync ? "vsync (sync interval 1)" : "immediate (sync interval 0)",
        (!ctx.present.vsync && ctx.present.tearingSupported) ? " + allow-tearing" : "");
    if (ctx.frame)
    {
        const FrameStats& f = *ctx.frame;
        o.P("| Frames rendered | %llu |\n", (unsigned long long)f.frames);
        o.P("| Frame rate | %.1f fps mean, %.1f fps 1%% low |\n",
            f.AvgMs() > 0 ? 1000.0 / f.AvgMs() : 0.0, f.Low1Fps());
        o.P("| Frame time | mean %.3f ms, sd %.3f ms, p99 %.3f ms |\n",
            f.AvgMs(), f.frameMs.Sd(), f.P99Ms());
    }
    o.P("| Presents dropped | %s |\n",
        (ctx.present.vsync && ctx.present.statsValid) ? "see JSON" : "n/a in immediate mode");
    o.S("\n");

    // ---- observations
    o.S("## Observations\n\n");
    const bool screenTiming = t.IntervalMs().n > 0;
    const bool padTiming = PadMeasured(ctx) && ctx.pad->IntervalMs().n > 0;
    if (!screenTiming)
    {
        if (GridScanRan(ctx))
        {
            o.S("No report timing was captured, so the rate cannot be assessed.\n\n");
            MdGridObservation(o, *ctx.grid);
            o.S("\n");
        }
        else if (!PadMeasured(ctx))
            o.S("No touch data was captured, so no assessment can be made.\n\n");
    }
    else
    {
        if (PadMeasured(ctx)) o.S("**Touch screen**\n\n");
        MdRateBullets(o, t);
        if (t.LatencyMs().n)
            o.P("- Median delivery latency %.2f ms, p99 %.2f ms.\n",
                lh.Percentile(0.50), lh.Percentile(0.99));
        if (t.PredictPx().n && t.PredictPx().mean > 0.5)
            o.P("- The OS moves reported positions by %.2f px on average.\n", t.PredictPx().mean);
        else if (t.PredictPx().n)
            o.S("- The OS applies no measurable smoothing or prediction to reported positions.\n");
        if (t.MaxSimultaneous() < 10)
            o.P("- Only %d simultaneous contacts were reached; press all ten fingers to confirm\n"
                "  the full contact count.\n", t.MaxSimultaneous());
        if (GridScanRan(ctx)) MdGridObservation(o, *ctx.grid);
        o.S("\n");
    }
    if (PadMeasured(ctx)) MdPadObservations(o, ctx);
    if (screenTiming || padTiming)
        o.S("Thresholds above are this tool's reporting conventions, not a standard.\n\n");

    // ---- hardware inventory
    o.S("## Touch hardware\n\n");
    if (ctx.devices && !ctx.devices->empty())
        for (size_t i = 0; i < ctx.devices->size(); ++i)
            MdDevice(o, (*ctx.devices)[i],
                     (int)i == ctx.activeDevice ? " — active"
                     : ((int)i == ctx.padDevice && PadMeasured(ctx) ? " — touch pad measured" : ""));
    else
        o.S("No touch digitizer was enumerated.\n\n");
    o.P("System reports touch support: %s. `SM_MAXIMUMTOUCHES` = %d.\n\n",
        SystemHasTouch() ? "yes" : "no", SystemMaxTouches());

    // ---- raw files
    o.S("## Raw data\n\n");
    o.S("Full measurements are in [`raw/`](raw/), and the machine-readable figures in\n"
        "[`summary.json`](summary.json).\n\n");
    // With a touch pad in the run, say which input each file belongs to.
    const bool padToo = PadMeasured(ctx);
    auto row = [&](const char* file, const char* who, const char* what) {
        std::string w = what;
        if (who) { w[0] = (char)tolower((unsigned char)w[0]); w = std::string(who) + ": " + w; }
        o.P("| [`raw/%s`](raw/%s) | %s |\n", file, file, w.c_str());
    };
    const char* screen = padToo ? "Touch screen" : nullptr;
    o.S("| File | Contents |\n| --- | --- |\n");
    row("samples.csv.gz", screen, "Every buffered sample: timestamps, interval, latency, coordinates, pressure — gzip");
    row("rate_by_contacts.csv", screen, "Report rate per simultaneous contact count");
    row("undelivered_touches.csv", screen, "Touches the panel reported that Windows did not deliver");
    if (GridScanRan(ctx))
        row("grid_coverage.csv", "Grid scan", "Every cell's position and how many samples landed in it");
    row("contacts.csv", screen, "Per-contact summary");
    row("interval_histogram.csv", screen, "Report-interval distribution");
    row("latency_histogram.csv", screen, "Delivery-latency distribution");
    if (padToo)
    {
        row("touchpad_samples.csv.gz", "Touch pad", "Every buffered sample, in pad units — gzip");
        row("touchpad_rate_by_contacts.csv", "Touch pad", "Report rate per simultaneous contact count");
        row("touchpad_contacts.csv", "Touch pad", "Per-contact summary");
        row("touchpad_interval_histogram.csv", "Touch pad", "Report-interval distribution");
    }
    row("frametime_histogram.csv", nullptr, "Render frame-time distribution");
    o.S("\n");
    o.P("`samples.csv.gz` is gzip-compressed CSV; `pandas.read_csv` and R's `read.csv`\n"
        "open it directly, as does 7-Zip. All contacts belonging to one hardware report\n"
        "share a `frame_id` and `device_qpc`. The `from_history` column marks samples\n"
        "recovered from coalesced frames; their `latency_ms` is not meaningful, so filter\n"
        "on `from_history=0` for latency analysis.\n\n");
    if (PadMeasured(ctx))
        o.S("`touchpad_samples.csv.gz` has one row per finger per pad frame; `x` and `y` are in\n"
            "the pad's own units. `device_ms` runs on the pad's scan-time clock, re-anchored to\n"
            "the host clock at the start of each touch, and `host_ms` is when the report arrived.\n\n");
    o.P("---\n\nGenerated by TouchRate. QPC frequency %lld Hz.\n", (long long)QpcFreq());
    return true;
}
// --------------------------------------------------------------- JSON summary

static bool WriteJson(const std::wstring& path, const ExportContext& ctx)
{
    Out o;
    if (!o.Open(path)) return false;
    const Tracker& t = *ctx.tracker;
    const Histogram& ih = t.IntervalHist();
    const Histogram& lh = t.LatencyHist();

    auto stat = [&](const char* name, const Stats& s, bool comma = true) {
        o.P("    \"%s\": {\"n\": %llu", name, (unsigned long long)s.n);
        if (s.n) o.P(", \"mean\": %.6f, \"sd\": %.6f, \"min\": %.6f, \"max\": %.6f",
                     s.mean, s.Sd(), s.mn, s.mx);
        o.P("}%s\n", comma ? "," : "");
    };
    // Rate at each contact count, closing the object it sits in.
    auto byCount = [&](const Tracker& tr) {
        o.S("    \"by_contact_count\": [\n");
        bool first = true;
        for (int n = 1; n <= kMaxSlots; ++n)
        {
            if (!tr.HasDataAt(n)) continue;
            const Stats& s = tr.IntervalMsAt(n);
            if (!first) o.S(",\n");
            first = false;
            o.P("      {\"contacts\": %d, \"modal_hz\": %.4f, \"mean_hz\": %.4f,"
                " \"interval_mean_ms\": %.6f, \"interval_sd_ms\": %.6f,"
                " \"interval_min_ms\": %.6f, \"interval_max_ms\": %.6f,"
                " \"max_gap_ms\": %.6f, \"samples\": %llu}",
                n, tr.ModeHzAt(n), tr.MeanHzAt(n), s.mean, s.Sd(), s.mn, s.mx,
                tr.MaxGapMsAt(n), (unsigned long long)s.n);
        }
        if (!first) o.S("\n");
        o.S("    ]\n");
    };

    o.S("{\n");
    o.P("  \"tool\": \"TouchRate\",\n");
    o.P("  \"generated\": \"%s\",\n", WideToUtf8(IsoNow()).c_str());
    o.P("  \"qpc_frequency\": %lld,\n", (long long)QpcFreq());
    o.P("  \"session_seconds\": %.3f,\n", QpcToSec(ctx.nowQpc - ctx.sessionStartQpc));
    o.P("  \"touching_seconds\": %.3f,\n", t.TouchingSec());

    o.S("  \"devices\": [\n");
    if (ctx.devices)
        for (size_t i = 0; i < ctx.devices->size(); ++i)
        {
            const TouchDevice& d = (*ctx.devices)[i];
            o.S("    {");
            o.P("\"index\": %zu, \"active\": %s, ", i, (int)i == ctx.activeDevice ? "true" : "false");
            o.P("\"name\": \"%s\", ", Esc(d.Label()).c_str());
            o.P("\"manufacturer\": \"%s\", ", Esc(WideToUtf8(d.manufacturer)).c_str());
            o.P("\"vid\": %u, \"pid\": %u, ", d.vid, d.pid);
            o.P("\"vid_hex\": \"%04X\", \"pid_hex\": \"%04X\", ", d.vid, d.pid);
            o.P("\"version\": %u, ", d.version);
            o.P("\"type\": \"%s\", ", Esc(WideToUtf8(d.typeName)).c_str());
            o.P("\"hid_usage_page\": %u, \"hid_usage\": %u, ", d.usagePage, d.usage);
            o.P("\"max_contacts_os\": %u, \"max_contacts_hid\": %u, ", d.maxContacts, d.hidMaxContacts);
            o.P("\"input_report_bytes\": %u, ", d.inputReportBytes);
            if (d.haveRects)
            {
                o.P("\"device_extent\": [%ld, %ld], ",
                    d.deviceRect.right - d.deviceRect.left, d.deviceRect.bottom - d.deviceRect.top);
                o.P("\"display_extent\": [%ld, %ld], ",
                    d.displayRect.right - d.displayRect.left, d.displayRect.bottom - d.displayRect.top);
                o.P("\"steps_per_pixel\": [%.6f, %.6f], ", d.StepsPerPixelX(), d.StepsPerPixelY());
            }
            o.P("\"path\": \"%s\"", Esc(WideToUtf8(d.path)).c_str());
            o.P("}%s\n", i + 1 < ctx.devices->size() ? "," : "");
        }
    o.S("  ],\n");

    o.S("  \"display\": {\n");
    if (ctx.monitor)
    {
        const MonitorInfo& m = *ctx.monitor;
        o.P("    \"monitor\": \"%s\",\n", Esc(WideToUtf8(m.friendlyName)).c_str());
        o.P("    \"gdi_device\": \"%s\",\n", Esc(WideToUtf8(m.gdiName)).c_str());
        o.P("    \"width\": %d, \"height\": %d, \"dpi\": %u,\n", m.width, m.height, m.dpi);
        o.P("    \"nominal_hz\": %.6f, \"nominal_exact\": %s,\n",
            m.nominalHz, m.nominalExact ? "true" : "false");
        o.P("    \"mode_table_hz\": %d,\n", m.modeHz);
        o.P("    \"dwm_hz\": %.6f, \"dwm_period_ms\": %.6f,\n", m.dwmHz, m.dwmPeriodMs);
    }
    bool vbv = ctx.vblank && ctx.vblank->Valid();
    o.P("    \"measured_vblank_valid\": %s", vbv ? "true" : "false");
    if (vbv)
        o.P(",\n    \"measured_vblank_hz\": %.6f, \"measured_vblank_period_ms\": %.6f,"
            " \"measured_vblank_sd_ms\": %.6f, \"vblanks\": %llu",
            ctx.vblank->Hz(), ctx.vblank->PeriodMs(), ctx.vblank->JitterMs(),
            (unsigned long long)ctx.vblank->Count());
    o.S("\n  },\n");

    o.S("  \"render\": {\n");
    o.P("    \"adapter\": \"%s\",\n", Esc(ctx.present.adapter).c_str());
    o.P("    \"vsync\": %s, \"tearing_supported\": %s,\n",
        ctx.present.vsync ? "true" : "false", ctx.present.tearingSupported ? "true" : "false");
    o.P("    \"buffer_count\": %u, \"max_frame_latency\": %u,\n",
        ctx.present.bufferCount, ctx.present.maxFrameLatency);
    if (ctx.frame)
    {
        const FrameStats& f = *ctx.frame;
        o.P("    \"frames\": %llu, \"fps_now\": %.3f, \"fps_mean\": %.3f, \"fps_1pct_low\": %.3f,\n",
            (unsigned long long)f.frames, f.windowHz,
            f.AvgMs() > 0 ? 1000.0 / f.AvgMs() : 0.0, f.Low1Fps());
        o.P("    \"frame_ms_mean\": %.6f, \"frame_ms_sd\": %.6f, \"frame_ms_p99\": %.6f,\n",
            f.AvgMs(), f.frameMs.Sd(), f.P99Ms());
    }
    o.P("    \"presents_dropped\": %lld\n", (long long)ctx.present.dropped);
    o.S("  },\n");

    o.S("  \"touch_rate\": {\n");
    o.P("    \"history_recovery\": %s,\n", t.UseHistory() ? "true" : "false");
    o.P("    \"frames\": %llu, \"samples\": %llu, \"history_samples\": %llu,\n",
        (unsigned long long)t.TotalFrames(), (unsigned long long)t.TotalSamples(),
        (unsigned long long)t.HistorySamples());
    o.P("    \"pointer_messages\": %llu, \"raw_hid_reports\": %llu,\n",
        (unsigned long long)t.Messages(), (unsigned long long)t.HidReports());
    o.P("    \"modal_hz\": %.4f, \"mean_hz\": %.4f, \"max_gap_ms\": %.4f,\n",
        t.ModeHz(), t.AvgHz(), t.MaxGapMs());
    if (ih.total)
        o.P("    \"interval_p50_ms\": %.6f, \"interval_p90_ms\": %.6f,"
            " \"interval_p99_ms\": %.6f, \"interval_p999_ms\": %.6f,\n",
            ih.Percentile(0.50), ih.Percentile(0.90), ih.Percentile(0.99), ih.Percentile(0.999));
    stat("interval_ms", t.IntervalMs());
    byCount(t);
    o.S("  },\n");

    o.S("  \"latency\": {\n");
    if (lh.total)
        o.P("    \"p50_ms\": %.6f, \"p90_ms\": %.6f, \"p99_ms\": %.6f, \"p999_ms\": %.6f,\n",
            lh.Percentile(0.50), lh.Percentile(0.90), lh.Percentile(0.99), lh.Percentile(0.999));
    stat("device_to_app_ms", t.LatencyMs(), false);
    o.S("  },\n");

    o.S("  \"multitouch\": {\n");
    o.P("    \"max_simultaneous\": %d,\n", t.MaxSimultaneous());
    o.S("    \"counts_reached\": [");
    { bool first = true;
      for (int i = 1; i <= kMaxSlots; ++i) if (t.SawSimultaneous(i)) { o.P("%s%d", first ? "" : ", ", i); first = false; } }
    o.S("],\n");
    o.P("    \"downs\": %llu, \"ups\": %llu, \"watchdog_released\": %llu,\n",
        (unsigned long long)t.Downs(), (unsigned long long)t.Ups(),
        (unsigned long long)t.LostContacts());
    stat("tap_interval_ms", t.TapIntervalMs());
    stat("dwell_ms", t.DwellMs(), false);
    o.S("  },\n");

    o.S("  \"position_processing\": {\n");
    stat("processed_minus_raw_px", t.PredictPx(), false);
    o.S("  },\n");

    o.S("  \"delivery\": {\n");
    o.P("    \"hid_reports\": %llu, \"hid_frames\": %llu,\n",
        (unsigned long long)t.HidReports(), (unsigned long long)t.HidFrames());
    o.P("    \"hid_touch_reports\": %llu, \"hid_no_tip_reports\": %llu, \"hid_empty_reports\": %llu,\n",
        (unsigned long long)t.HidTouchReports(), (unsigned long long)t.HidNoTipReports(),
        (unsigned long long)t.HidEmptyReports());
    o.P("    \"panel_contacts\": %llu, \"delivered\": %llu, \"undelivered\": %llu,\n",
        (unsigned long long)t.HidContacts(), (unsigned long long)t.HidDelivered(),
        (unsigned long long)t.HidUndelivered());
    o.P("    \"edge_band_px\": %.0f,\n", kEdgeBandPx);
    {
        const WindowInfo& w = ctx.window;
        o.P("    \"window\": {\"left\": %ld, \"top\": %ld, \"width\": %ld, \"height\": %ld, "
            "\"fullscreen\": %s, \"edge_swipe_blocked\": %s},\n",
            w.client.left, w.client.top, w.client.right - w.client.left, w.client.bottom - w.client.top,
            w.fullscreen ? "true" : "false", w.edgeSwipeBlocked ? "true" : "false");
    }
    {
        HidDescriptorInfo di;
        if (ctx.hid && ctx.hid->LastScreenDevice() && ctx.hid->Describe(ctx.hid->LastScreenDevice(), di))
            o.P("    \"hid_descriptor\": {\"contact_slots\": %u, \"contact_count\": %s, \"contact_id\": %s, "
                "\"tip_switch\": %s, \"confidence\": %s, \"x_min\": %d, \"x_max\": %d, "
                "\"y_min\": %d, \"y_max\": %d, \"report_id\": %u},\n",
                di.fingerCollections, di.hasContactCount ? "true" : "false",
                di.hasContactId ? "true" : "false", di.hasTipSwitch ? "true" : "false",
                di.hasConfidence ? "true" : "false", di.xMin, di.xMax, di.yMin, di.yMax, di.reportId);
    }
    {
        size_t grouped = 0;
        const size_t groups = FingerGroups(t.Undelivered(), grouped);
        o.P("    \"undelivered_finger_groups\": %zu, \"undelivered_in_finger_groups\": %zu,\n",
            groups, grouped);
    }
    stat("match_offset_px", t.HidMatchOffsetPx());
    stat("delivered_edge_distance_px", t.DeliveredEdgeDist());
    stat("undelivered_edge_distance_px", t.UndeliveredEdgeDist(), false);
    o.S("  },\n");

    if (PadMeasured(ctx))
    {
        const Tracker& p = *ctx.pad;
        const TouchPadInput& in = *ctx.padIn;
        const HidDescriptorInfo& d = in.Desc();
        const Histogram& ph = p.IntervalHist();
        o.S("  \"touchpad\": {\n");
        if (const TouchDevice* dev = PadDevice(ctx))
            o.P("    \"device_index\": %d, \"name\": \"%s\", \"vid_hex\": \"%04X\", \"pid_hex\": \"%04X\",\n",
                (int)(dev - ctx.devices->data()), Esc(dev->Label()).c_str(), dev->vid, dev->pid);
        o.P("    \"logical_x\": [%d, %d], \"logical_y\": [%d, %d],", d.xMin, d.xMax, d.yMin, d.yMax);
        if (d.widthMm > 0 && d.heightMm > 0) o.P(" \"size_mm\": [%.2f, %.2f],", d.widthMm, d.heightMm);
        o.P("\n    \"contact_slots_per_report\": %u, \"hybrid\": %s, \"max_declared_contacts\": %u,\n",
            d.fingerCollections, in.Hybrid() ? "true" : "false", in.MaxDeclared());
        o.P("    \"timing\": \"%s\", \"scan_time_reported\": %s, \"scan_time_checked\": %s,"
            " \"scan_time_ratio\": %.6f, \"scan_time_rejected\": %s,\n",
            in.ScanClock() ? "scan_time" : "arrival", d.hasScanTime ? "true" : "false",
            in.ScanChecked() ? "true" : "false", in.ScanRatio(), in.ScanRejected() ? "true" : "false");
        o.P("    \"hid_reports\": %llu, \"frames\": %llu, \"samples\": %llu,\n",
            (unsigned long long)in.Reports(), (unsigned long long)p.TotalFrames(),
            (unsigned long long)p.TotalSamples());
        o.P("    \"modal_hz\": %.4f, \"mean_hz\": %.4f, \"max_gap_ms\": %.4f,\n",
            p.ModeHz(), p.AvgHz(), p.MaxGapMs());
        if (ph.total)
            o.P("    \"interval_p50_ms\": %.6f, \"interval_p90_ms\": %.6f,"
                " \"interval_p99_ms\": %.6f, \"interval_p999_ms\": %.6f,\n",
                ph.Percentile(0.50), ph.Percentile(0.90), ph.Percentile(0.99), ph.Percentile(0.999));
        stat("interval_ms", p.IntervalMs());
        stat("arrival_interval_ms", in.ArrivalIntervalMs());
        stat("added_jitter_ms", in.AddedJitterMs());
        o.P("    \"clock_step_ms\": %g, \"bunched_reports\": %llu, \"arrival_steps\": %llu,\n",
            in.ClockStepMs(), (unsigned long long)in.Bunched(), (unsigned long long)in.ArrivalSteps());
        stat("bunch_wait_ms", in.BunchWaitMs());
        o.P("    \"max_simultaneous\": %d,\n", p.MaxSimultaneous());
        o.S("    \"counts_reached\": [");
        { bool first = true;
          for (int i = 1; i <= kMaxSlots; ++i) if (p.SawSimultaneous(i)) { o.P("%s%d", first ? "" : ", ", i); first = false; } }
        o.S("],\n");
        o.P("    \"downs\": %llu, \"ups\": %llu, \"watchdog_released\": %llu,"
            " \"palm_contacts\": %llu, \"button_clicks\": %llu,\n",
            (unsigned long long)p.Downs(), (unsigned long long)p.Ups(),
            (unsigned long long)p.LostContacts(), (unsigned long long)in.Palms(),
            (unsigned long long)in.Clicks());
        stat("tap_interval_ms", p.TapIntervalMs());
        stat("dwell_ms", p.DwellMs());
        byCount(p);
        o.S("  },\n");
    }

    if (GridScanRan(ctx))
    {
        const GridScan& g = *ctx.grid;
        const RECT& a = g.Area();
        GridScan::Side left, top, right, bottom;
        g.EdgeSummary(left, top, right, bottom);
        o.S("  \"grid_scan\": {\n");
        o.P("    \"cols\": %d, \"rows\": %d, \"cells\": %d, \"touched\": %d, \"coverage\": %.6f,\n",
            g.Cols(), g.Rows(), g.Cells(), g.Touched(), g.Coverage());
        o.P("    \"area_px\": [%ld, %ld], \"samples\": %llu, \"samples_truncated\": %s,\n",
            a.right - a.left, a.bottom - a.top, (unsigned long long)g.Samples(),
            g.Truncated() ? "true" : "false");
        o.P("    \"edge_untouched\": {\"left\": [%d, %d], \"top\": [%d, %d], "
            "\"right\": [%d, %d], \"bottom\": [%d, %d]},\n",
            left.untouched, left.cells, top.untouched, top.cells,
            right.untouched, right.cells, bottom.untouched, bottom.cells);
        o.S("    \"untouched_cells\": [");
        bool first = true;
        for (int r = 0; r < g.Rows(); ++r)
            for (int c = 0; c < g.Cols(); ++c)
                if (!g.Hits(c, r)) { o.P("%s[%d, %d]", first ? "" : ", ", c, r); first = false; }
        o.S("]\n  },\n");
    }

    o.P("  \"exported_sample_rows\": %llu,\n", (unsigned long long)t.RecordCount());
    o.P("  \"samples_dropped_from_ring\": %llu\n", (unsigned long long)t.RecordsDropped());
    o.S("}\n");
    return true;
}

// ---------------------------------------------------------------------- driver

// ---------------------------------------------------------------------- driver

ExportResult ExportAll(const ExportContext& ctx, const std::wstring& baseDir)
{
    ExportResult res;
    if (!ctx.tracker) { res.error = "no tracker"; return res; }

    // One folder per run: a rendered summary at the top, machine-readable
    // figures beside it, and the bulky measurements in raw/.
    //
    //   touchrate_<stamp>/
    //     README.md        rendered by GitHub and friends on folder open
    //     summary.json
    //     raw/*.csv
    const std::wstring root = baseDir.empty() ? DefaultExportDir() : baseDir;
    res.stem = L"touchrate_" + TimeStampString();
    res.dir = root + L"\\" + res.stem;
    const std::wstring raw = res.dir + L"\\raw";

    if (!EnsureDirectory(res.dir) || !EnsureDirectory(raw))
    {
        res.error = "could not create the export folder";
        return res;
    }

    std::wstring p = raw + L"\\samples.csv.gz";
    if (!WriteSamples(p, *ctx.tracker, res.sampleRows, res.sampleBytesRaw, res.sampleBytesGz))
    {
        res.error = "failed writing samples.csv.gz";
        return res;
    }
    res.files.push_back(p);

    p = raw + L"\\contacts.csv";
    if (WriteContacts(p, *ctx.tracker)) res.files.push_back(p);

    p = raw + L"\\rate_by_contacts.csv";
    if (WriteRateByCount(p, *ctx.tracker)) res.files.push_back(p);

    p = raw + L"\\undelivered_touches.csv";
    {
        Out o;
        if (o.Open(p))
        {
            o.S("index,at_s,start_x,start_y,end_x,end_y,duration_ms,reports,"
                "edge_distance_px,edge_swipe_blocked,position_mapped\n");
            const std::vector<UndeliveredTouch>& und = ctx.tracker->Undelivered();
            for (size_t i = 0; i < und.size(); ++i)
            {
                const UndeliveredTouch& u = und[i];
                o.P("%zu,%.4f,%.1f,%.1f,%.1f,%.1f,%.2f,%u,", i + 1,
                    QpcToSec(u.qpc - ctx.sessionStartQpc), u.x, u.y, u.lastX, u.lastY,
                    u.durationMs, u.reports);
                if (u.mapped) o.P("%.1f,", u.edgeDistPx); else o.S(",");
                o.P("%d,%d\n", u.edgeSwipeBlocked ? 1 : 0, u.mapped ? 1 : 0);
            }
            res.files.push_back(p);
        }
    }

    if (GridScanRan(ctx))
    {
        // Positions relative to the monitor's top-left, so the file does not
        // depend on where that monitor sat in the desktop layout.
        p = raw + L"\\grid_coverage.csv";
        Out o;
        if (o.Open(p))
        {
            const GridScan& g = *ctx.grid;
            const RECT& a = g.Area();
            o.S("col,row,x0,y0,x1,y1,hits,touched\n");
            for (int r = 0; r < g.Rows(); ++r)
                for (int c = 0; c < g.Cols(); ++c)
                {
                    const RECT rc = g.CellRect(c, r);
                    const uint32_t n = g.Hits(c, r);
                    o.P("%d,%d,%ld,%ld,%ld,%ld,%u,%d\n", c, r,
                        rc.left - a.left, rc.top - a.top, rc.right - a.left, rc.bottom - a.top,
                        n, n ? 1 : 0);
                }
            res.files.push_back(p);
        }
    }

    if (PadMeasured(ctx))
    {
        // The touch pad's files sit beside the screen's, prefixed so neither
        // can be mistaken for the other.
        p = raw + L"\\touchpad_samples.csv.gz";
        if (!WritePadSamples(p, *ctx.pad, res.padSampleRows))
        {
            res.error = "failed writing touchpad_samples.csv.gz";
            return res;
        }
        res.files.push_back(p);
        p = raw + L"\\touchpad_rate_by_contacts.csv";
        if (WriteRateByCount(p, *ctx.pad)) res.files.push_back(p);
        p = raw + L"\\touchpad_contacts.csv";
        if (WriteContacts(p, *ctx.pad)) res.files.push_back(p);
        p = raw + L"\\touchpad_interval_histogram.csv";
        { Out o; if (o.Open(p)) { HistogramCsv(o, ctx.pad->IntervalHist(), "interval_ms", true); res.files.push_back(p); } }
    }

    p = raw + L"\\interval_histogram.csv";
    { Out o; if (o.Open(p)) { HistogramCsv(o, ctx.tracker->IntervalHist(), "interval_ms", true); res.files.push_back(p); } }

    p = raw + L"\\latency_histogram.csv";
    { Out o; if (o.Open(p)) { HistogramCsv(o, ctx.tracker->LatencyHist(), "latency_ms", false); res.files.push_back(p); } }

    if (ctx.frame)
    {
        p = raw + L"\\frametime_histogram.csv";
        Out o; if (o.Open(p)) { HistogramCsv(o, ctx.frame->hist, "frame_ms", true); res.files.push_back(p); }
    }

    p = res.dir + L"\\summary.json";
    if (!WriteJson(p, ctx)) { res.error = "failed writing summary.json"; return res; }
    res.files.push_back(p);

    p = res.dir + L"\\README.md";
    if (!WriteMarkdown(p, ctx)) { res.error = "failed writing README.md"; return res; }
    res.files.push_back(p);
    res.summary = p;

    res.ok = true;
    return res;
}
