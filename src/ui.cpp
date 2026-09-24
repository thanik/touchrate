#include "app.h"
#include <cstdarg>

// ============================================================ layout helpers

namespace {

struct Layout
{
    // canvas covers the whole window so a contact is tracked and drawn wherever
    // it lands; canvasFree is the part no panel sits on, used for hint text.
    Rect2 header, canvas, canvasFree, stats, histo, graph, table, footer;
};

// Panels float over the touch canvas, so they are translucent enough to show
// ink and trails underneath without costing text contrast.
constexpr float kPanelA  = 0.82f;   // a panel sitting directly on the canvas
constexpr float kColumnA = 0.58f;   // the stats column background
constexpr float kBlockA  = 0.55f;   // a block nested inside that column
constexpr float kBarA    = 0.88f;   // header and footer

// Layout sizes at 96 DPI, scaled by the renderer's DPI factor.
constexpr float kHeaderH   = 86.f;
constexpr float kFooterH   = 26.f;
constexpr float kStripMin  = 140.f;   // natural minimum of the graph strip
constexpr float kStripMax  = 200.f;

// Per-contact table geometry, shared by the layout that sizes the strip and
// the code that draws the table.
constexpr float kTableTop    = 24.f;   // block title above the column headers
constexpr float kTableBottom = 5.f;
constexpr int   kTableRows   = 10;     // one row per finger, at normal spacing

float TableLine(const Renderer& r) { return std::round(r.FontHeight(F_TINY) * 1.18f); }

// Height that holds the column headers plus 'rows' rows at normal spacing.
float TableHeight(const Renderer& r, int rows)
{
    return std::ceil((kTableTop + kTableBottom) * r.Scale() + (rows + 1) * TableLine(r));
}
// Wide enough that the stats column, at 36% of the window, fits its longest
// value ("120.002 Hz  +/-0.018 ms") without shortening it.
constexpr float kMinClientW = 1240.f;

// Longest prefix of 'text' that fits in maxW, ending in "..." if cut.
std::string FitText(const Renderer& r, int font, const std::string& text, float maxW)
{
    if (r.TextW(font, text.c_str()) <= maxW) return text;
    std::string t = text;
    while (!t.empty() && r.TextW(font, (t + "...").c_str()) > maxW) t.pop_back();
    while (!t.empty() && t.back() == ' ') t.pop_back();   // not "TITLE   ..."
    return t.empty() ? std::string() : t + "...";
}

// Vertical metrics of the stats column. Computed in one place so the layout that
// sizes the column and the code that draws it cannot disagree.
struct StatsMetrics
{
    float pad, rowH, titleH, chrome, gap, pipR;
    float hTiming, hDeliver, hDisplay, hMulti, hCounters;
    float heroMin, heroMax;
    float fixedH;                 // everything except the hero and counters
    float Need() const { return fixedH + heroMin; }
};

// Compact spacing is for short screens - a 1080p panel at 125% scaling cannot
// hold the natural spacing, and the column would otherwise run under the strip.
StatsMetrics MeasureStats(const Renderer& r, bool compact)
{
    const float s = r.Scale();
    StatsMetrics m{};
    m.pad    = (compact ? 8 : 12) * s;
    m.rowH   = std::round(r.FontHeight(F_BODY) * (compact ? 1.02f : 1.18f));
    m.titleH = std::round(r.FontHeight(F_TINY) * (compact ? 1.35f : 1.6f) + (compact ? 2 : 4) * s);
    m.chrome = m.titleH + (compact ? 3 : 6) * s;
    m.gap    = compact ? std::round(3 * s) : std::round(m.pad * 0.55f);
    m.pipR   = (compact ? 9 : 11) * s;

    m.hTiming  = m.rowH * 6 + m.chrome;
    m.hDeliver = m.rowH * 5 + m.chrome;
    m.hDisplay = m.rowH * 5 + m.chrome;
    // Extra line under the pips carries the measured rate at each contact count.
    m.hMulti   = m.titleH + m.rowH * 2 + 4 * s + m.pipR * 2
               + r.FontHeight(F_TINY) * 1.35f + (compact ? 4 : 8) * s;
    m.hCounters = r.FontHeight(F_TINY) * 1.25f * 2 + 6 * s;
    // The small face still needs room for the session / gaps pair beside it.
    m.heroMin  = std::max(r.FontHeight(F_HEAD), r.FontHeight(F_BODY) * 2.5f) + m.chrome;
    m.heroMax  = r.FontHeight(F_HERO) + m.chrome;
    m.fixedH   = m.hTiming + m.hDeliver + m.hDisplay + m.hMulti + m.gap * 4 + m.pad * 2;
    return m;
}

Layout ComputeLayout(const Renderer& r)
{
    const float s = r.Scale();
    const float W = (float)r.Width(), H = (float)r.Height();

    Layout L;
    const float headerH = std::round(kHeaderH * s);
    const float footerH = std::round(kFooterH * s);
    const float bodyH = H - headerH - footerH;
    // Never shorter than ten per-contact rows at normal spacing.
    const float naturalStrip = std::max(TableHeight(r, kTableRows),
        std::round(std::min(kStripMax * s, std::max(kStripMin * s, H * 0.22f))));
    const float stripH = naturalStrip;
    const float statsW  = std::round(std::min(560.f * s, std::max(320.f * s, W * 0.36f)));

    // The strip cannot give up height without losing contact rows, so if the
    // stats column does not fit above it - only possible below the window's
    // minimum size, i.e. full screen on a small or high-DPI screen - the column
    // runs down to the footer and the strip stays under the canvas.
    const float compactNeed = MeasureStats(r, true).Need();
    const bool statsFull = bodyH - stripH < compactNeed;

    L.header = { 0, 0, W, headerH };
    L.footer = { 0, H - footerH, W, footerH };

    const float midY = headerH;
    const float midH = std::max(40.f, bodyH - stripH);

    L.canvas     = { 0, 0, W, H };
    L.canvasFree = { 0, midY, W - statsW, midH };
    L.stats      = { W - statsW, midY, statsW, statsFull ? bodyH : midH };

    const float stripY = midY + midH;
    const float stripW = statsFull ? W - statsW : W;
    const float tableW = std::round(std::min(470.f * s, stripW * 0.34f));
    const float restW  = stripW - tableW;
    L.histo = { 0, stripY, std::round(restW * 0.5f), stripH };
    L.graph = { L.histo.r(), stripY, restW - L.histo.w, stripH };
    L.table = { L.graph.r(), stripY, tableW, stripH };
    return L;
}

// A vertical label/value writer used by every stats block.
struct Col
{
    Renderer& r;
    float x, y, w;
    float lh;
    float labelW;

    Col(Renderer& rr, const Rect2& box, float labelFrac = 0.52f)
        : r(rr), x(box.x), y(box.y), w(box.w)
    {
        lh = std::round(r.FontHeight(F_BODY) * 1.18f);
        labelW = box.w * labelFrac;
    }
    void Row(const char* label, Color vc, const char* fmt, ...)
    {
        char buf[256];
        va_list ap; va_start(ap, fmt);
        _vsnprintf_s(buf, sizeof buf, _TRUNCATE, fmt, ap);
        va_end(ap);
        // The value never starts inside a long label, and is shortened rather
        // than run past the column when space is tight.
        const float vx = x + std::max(labelW, r.TextW(F_BODY, label) + 10 * r.Scale());
        r.Text(F_BODY, x, y, Pal::textDim, label);
        r.Text(F_BODY, vx, y, vc, FitText(r, F_BODY, buf, x + w - vx).c_str());
        y += lh;
    }
};

void Block(Renderer& r, const Rect2& box, const char* title, float alpha = kPanelA)
{
    r.FillRect(box, Pal::panel.WithA(alpha));
    r.FrameRect(box, 1.f, Pal::edge);
    if (title)
    {
        const float s = r.Scale();
        r.Text(F_TINY, box.x + 10 * s, box.y + 6 * s, Pal::accent,
               FitText(r, F_TINY, title, box.w - 20 * s).c_str());
    }
}

Color RateColor(double hz)
{
    if (hz <= 0)   return Pal::textFaint;
    if (hz < 60)   return Pal::bad;
    if (hz < 100)  return Pal::warn;
    return Pal::good;
}

Color JitterColor(double sd, double periodMs)
{
    if (periodMs <= 0 || sd <= 0) return Pal::textDim;
    double rel = sd / periodMs;
    if (rel > 0.35) return Pal::bad;
    if (rel > 0.15) return Pal::warn;
    return Pal::good;
}
} // namespace

// ================================================================ header

static void DrawHeader(App& app, const Rect2& box)
{
    Renderer& r = app.rend;
    const float s = r.Scale();
    r.FillRect(box, Pal::panel2.WithA(kBarA));
    r.FillRect(Rect2{ box.x, box.b() - 1, box.w, 1 }, Pal::edge);

    const float margin = 14 * s, gap = 16 * s;

    // Right side first, so the left side knows how far it may run. Each line
    // is limited to under half the header so a long monitor or adapter name
    // cannot crowd out the device identity.
    const MonitorInfo& m = app.monitor;
    std::string mon = m.friendlyName.empty() ? WideToUtf8(m.gdiName) : WideToUtf8(m.friendlyName);
    char rb[256];
    snprintf(rb, sizeof rb, "%s   %dx%d   %.3f Hz nominal   DPI %u",
             mon.c_str(), m.width, m.height, m.nominalHz, m.dpi);
    const std::string right1 = FitText(r, F_TINY, rb, box.w * 0.45f);
    const std::string right2 = FitText(r, F_TINY, r.AdapterName(), box.w * 0.45f);
    const float rw1 = r.TextW(F_TINY, right1.c_str()), rw2 = r.TextW(F_TINY, right2.c_str());
    r.TextRight(F_TINY, box.r() - margin, 10 * s, Pal::textDim, "%s", right1.c_str());
    r.TextRight(F_TINY, box.r() - margin, 10 * s + r.FontHeight(F_TINY) * 1.4f,
                Pal::textFaint, "%s", right2.c_str());

    // The title row sits level with both right-hand lines, the identity line
    // with the second; the capability line below them has the full width.
    const float limitTitle = box.r() - margin - std::max(rw1, rw2) - gap;
    const float limitIdent = box.r() - margin - rw2 - gap;
    const float limitFull  = box.r() - margin;

    float x = margin, y = 8 * s;
    r.Text(F_HEAD, x, y, Pal::hero, "TouchRate");
    const float sx = x + r.TextW(F_HEAD, "TouchRate") + 10 * s;
    r.Text(F_TINY, sx, y + r.FontHeight(F_HEAD) - r.FontHeight(F_TINY) - 2 * s, Pal::textFaint,
           FitText(r, F_TINY, "touch polling rate / latency / 10-finger analyzer", limitTitle - sx).c_str());

    y += std::round(r.FontHeight(F_HEAD) * 1.05f);
    const float lh = std::round(r.FontHeight(F_BODY) * 1.12f);

    if (app.devices.empty())
    {
        r.Text(F_BODY, x, y, Pal::bad,
               FitText(r, F_BODY, "No touch digitizer found.", limitIdent - x).c_str());
        r.Text(F_BODY, x, y + lh, Pal::textDim,
               FitText(r, F_BODY, "Connect a touch screen, then press [D] to re-enumerate.",
                       limitFull - x).c_str());
        return;
    }

    const bool pad = app.ShowingPad();
    const bool pen = app.ShowingPen();
    const TouchDevice* shown = app.ShownDevice();
    const TouchDevice& d = shown ? *shown : app.devices[0];

    // Line 1: identity. Segments are added in order of importance and stop
    // at the first that does not fit; the name itself is shortened instead.
    float cx = x;
    auto seg = [&](int font, Color c, const std::string& text, float dy) {
        const float tw = r.TextW(font, text.c_str());
        if (cx + tw > limitIdent) return false;
        r.Text(font, cx, y + dy, c, text.c_str());
        cx += tw + gap;
        return true;
    };
    const std::string name = FitText(r, F_BODY, d.Label(), (limitIdent - x) * 0.6f);
    r.Text(F_BODY, cx, y, Pal::text, name.c_str());
    cx += r.TextW(F_BODY, name.c_str()) + gap;

    char vb[32] = "";
    if (d.version) snprintf(vb, sizeof vb, "rev 0x%04X", d.version);
    char mb[64] = "";
    // Before any report the shown device is a guess; once reports arrive it is
    // not "no reports", even when their device could not be told apart.
    const bool penSeen = app.pen.TotalFrames() > 0 || app.pen.HoverReports() > 0;
    const bool screenSeen = app.activeDevice >= 0 || app.tracker.TotalFrames() > 0;
    if (pad ? !app.padIn.Seen() : pen ? (!penSeen && app.devices.size() > 1)
                                      : (!screenSeen && app.devices.size() > 1))
        snprintf(mb, sizeof mb, pad ? "(no reports yet - touch the pad)"
                              : pen ? "(no reports yet - bring the pen to the screen)"
                                    : "(no reports yet - touch the screen)");
    else if (app.devices.size() > 1)
        snprintf(mb, sizeof mb, "+%zu more digitizer%s", app.devices.size() - 1,
                 app.devices.size() == 2 ? "" : "s");

    if (seg(F_BODY, Pal::accent, d.VidPidString(), 0) &&
        (!vb[0] || seg(F_BODY, Pal::textDim, vb, 0)) &&
        seg(F_BODY, Pal::textDim, WideToUtf8(d.typeName), 0) && mb[0])
        seg(F_TINY, Pal::textFaint, mb, 2 * s);

    // Line 2: capability and geometry
    y += lh;
    char buf[512];
    int n = 0;
    if (pen)
    {
        // What the pen itself resolves; applications get 0..1024 regardless.
        if (d.pressureLevels)
            n += snprintf(buf + n, sizeof buf - n, "pressure %u levels (0-1024 to apps)", d.pressureLevels);
        else
            n += snprintf(buf + n, sizeof buf - n, "pressure 0-1024");
        n += snprintf(buf + n, sizeof buf - n, "   tilt %s", d.hasTilt ? "yes" : "no");
        if (d.hasTwist) n += snprintf(buf + n, sizeof buf - n, "   twist yes");
    }
    else
    {
        n += snprintf(buf + n, sizeof buf - n, "contacts %u", d.ContactCapacity());
        if (d.hidMaxContacts && d.hidMaxContacts < d.ContactCapacity())
            n += snprintf(buf + n, sizeof buf - n, " (%u per HID report)", d.hidMaxContacts);
    }
    if (d.haveRects)
    {
        n += snprintf(buf + n, sizeof buf - n, "   digitizer %ldx%ld units -> %ldx%ld px",
                      d.deviceRect.right - d.deviceRect.left,
                      d.deviceRect.bottom - d.deviceRect.top,
                      d.displayRect.right - d.displayRect.left,
                      d.displayRect.bottom - d.displayRect.top);
        n += snprintf(buf + n, sizeof buf - n, "   %.2f steps/px",
                      d.StepsPerPixelX());
    }
    // A touch pad is not mapped to a display; its own surface is what it has.
    HidDescriptorInfo pd;
    bool havePd = false;
    if (pad && d.rawHandle)
    {
        if (app.padIn.Device() == d.rawHandle && app.padIn.Desc().valid) { pd = app.padIn.Desc(); havePd = true; }
        else havePd = app.hid.Describe(d.rawHandle, pd);
    }
    if (havePd && pd.xMax > pd.xMin)
    {
        n += snprintf(buf + n, sizeof buf - n, "   pad %dx%d units", pd.xMax - pd.xMin, pd.yMax - pd.yMin);
        if (pd.widthMm > 0 && pd.heightMm > 0)
            n += snprintf(buf + n, sizeof buf - n, " = %.0fx%.0f mm", pd.widthMm, pd.heightMm);
        n += snprintf(buf + n, sizeof buf - n, "   scan time %s", pd.hasScanTime ? "yes" : "no");
    }
    if (d.inputReportBytes)
        n += snprintf(buf + n, sizeof buf - n, "   report %u B", d.inputReportBytes);
    r.Text(F_TINY, x, y + 2 * s, Pal::textDim, FitText(r, F_TINY, buf, limitFull - x).c_str());
}

// ================================================================ canvas

static void DrawCanvas(App& app, const Rect2& box)
{
    Renderer& r = app.rend;
    Tracker& t = app.tracker;
    const float s = r.Scale();
    const int64_t now = app.nowQpc;

    r.FillRect(box, Pal::bg);
    r.SetClip(box);

    // The grid is spatial reference for the open canvas only. Ink, trails and
    // contacts still span the whole window so they show through the panels.
    if (app.view.showGrid)
    {
        const Rect2& g = app.freeArea;
        const float step = std::round(100 * s);
        for (float gx = box.x + step; gx < box.r(); gx += step)
        {
            if (gx < g.x || gx > g.r()) continue;
            r.FillRect(Rect2{ std::round(gx), g.y, 1, g.h }, Pal::grid);
        }
        for (float gy = box.y + step; gy < box.b(); gy += step)
        {
            if (gy < g.y || gy > g.b()) continue;
            r.FillRect(Rect2{ g.x, std::round(gy), g.w, 1 }, Pal::grid);
        }
    }

    // Persistent ink. Only points that arrived since the last frame are drawn,
    // into an offscreen layer that is then composited, so the accumulated
    // session history costs the same per frame however long it gets.
    {
        // Points each tracker has added since the last frame, oldest first.
        auto freshInk = [](const Tracker& tr, uint64_t drawn, size_t& start) {
            uint64_t fresh = tr.InkTotal() - drawn;
            if (fresh > (uint64_t)tr.InkCount()) fresh = (uint64_t)tr.InkCount();
            const size_t cap = tr.Ink().size();
            start = cap ? (tr.InkHead() + cap - (size_t)fresh) % cap : 0;
            return (size_t)fresh;
        };
        const Tracker& pen = app.pen;
        size_t start = 0, penStart = 0;
        const size_t fresh = freshInk(t, app.inkDrawn, start);
        const size_t penFresh = freshInk(pen, app.penInkDrawn, penStart);

        if ((fresh || penFresh) && r.HasInkLayer())
        {
            const float dot = std::max(1.f, std::round(1.f * s));
            r.BeginInk();
            r.SetClip(box);
            const std::vector<InkPt>& ink = t.Ink();
            for (size_t i = 0; i < fresh; ++i)
            {
                const InkPt& p = ink[(start + i) % ink.size()];
                if (!box.Contains(p.x, p.y)) continue;
                r.FillRect(Rect2{ p.x, p.y, dot, dot },
                           SlotColor(p.slot).WithA(p.fromHistory ? 0.35f : 0.60f));
            }
            // A pen's ink is a dot per report sized by its pressure, so the
            // stroke thickens where it was pressed harder, and the spacing
            // of the dots still shows the report rate.
            const std::vector<InkPt>& pink = pen.Ink();
            for (size_t i = 0; i < penFresh; ++i)
            {
                const InkPt& p = pink[(penStart + i) % pink.size()];
                if (!box.Contains(p.x, p.y)) continue;
                const float k = p.pressure / 255.f;
                r.Disc(p.x, p.y, (0.6f + 2.4f * k) * s,
                       Pal::pen.WithA((0.30f + 0.45f * k) * (p.fromHistory ? 0.8f : 1.f)));
            }
            r.EndInk();
        }
        app.inkDrawn = t.InkTotal();
        app.penInkDrawn = pen.InkTotal();

        if (app.view.showInk && r.HasInkLayer())
        {
            r.SetClip(box);
            r.CompositeInk();
        }
    }

    // Live strokes. The point budget bounds the work when ten fingers report at
    // a high rate, so drawing never becomes the reason frame time rises.
    if (app.view.showTrails)
    {
        const double fadeMs = 1400.0;
        const size_t kBudget = 6000;
        size_t totalPts = 0;
        for (int i = 0; i < kMaxSlots; ++i) totalPts += t.Slot(i).trailCount;
        const size_t stride = totalPts > kBudget ? (totalPts / kBudget + 1) : 1;

        for (int i = 0; i < kMaxSlots; ++i)
        {
            const Contact& c = t.Slot(i);
            if (c.trailCount < 2) continue;
            const Color base = SlotColor(i);
            for (size_t k = stride; k < c.trailCount; k += stride)
            {
                const TrailPt& a = c.TrailAt(k - stride);
                const TrailPt& b = c.TrailAt(k);
                double age = QpcToMs(now - b.qpc);
                if (age > fadeMs) continue;
                float alpha = (float)(1.0 - age / fadeMs);
                r.Line(a.x, a.y, b.x, b.y, std::max(1.f, 1.6f * s), base.WithA(alpha * 0.65f));
                if (app.view.showDots)
                {
                    float rad = b.fromHistory ? 1.6f * s : 2.3f * s;
                    r.Disc(b.x, b.y, rad, base.WithA(alpha * (b.fromHistory ? 0.5f : 0.95f)));
                }
            }
        }

        // The pen's stroke, as wide as it was pressed hard.
        for (int i = 0; i < kMaxSlots; ++i)
        {
            const Contact& c = app.pen.Slot(i);
            for (size_t k = 1; k < c.trailCount; ++k)
            {
                const TrailPt& a = c.TrailAt(k - 1);
                const TrailPt& b = c.TrailAt(k);
                const double age = QpcToMs(now - b.qpc);
                if (age > fadeMs) continue;
                const float alpha = (float)(1.0 - age / fadeMs);
                const float p = b.pressure >= 0 ? Clampf(b.pressure, 0.f, 1.f) : 0.3f;
                r.Line(a.x, a.y, b.x, b.y, std::max(1.f, (0.8f + 4.4f * p) * s), Pal::pen.WithA(alpha * 0.75f));
                if (app.view.showDots)
                    r.Disc(b.x, b.y, (b.fromHistory ? 1.3f : 1.8f) * s,
                           Pal::accent.WithA(alpha * (b.fromHistory ? 0.5f : 0.9f)));
            }
        }
    }

    // Hints while nothing is down; a pen close to the screen counts as down.
    const bool penNear = app.pen.ContactsNow() > 0 || app.pen.Hovering(now);
    if (t.ContactsNow() == 0 && !penNear && !app.ShowingPad())
    {
        const Rect2& f = app.freeArea;
        const bool pen = app.ShowingPen();
        const char* msg = app.devices.empty() ? "No touch device detected"
                        : pen ? "Draw anywhere with the pen to begin measuring"
                              : "Touch anywhere to begin measuring";
        r.TextCenter(F_HEAD, f.x + f.w * 0.5f,
                     f.y + f.h * 0.5f - r.FontHeight(F_HEAD),
                     Pal::textFaint, "%s", msg);
        if (!app.devices.empty())
            r.TextCenter(F_TINY, f.x + f.w * 0.5f, f.y + f.h * 0.5f + 4 * s,
                         Pal::textFaint, "%s",
                         pen ? "Drag slowly to see sample spacing. Press harder and lighter to see the pressure."
                             : "Drag slowly to see sample spacing. Press all ten fingers for the contact test.");
        if (!pen && app.HasPen())
            r.TextCenter(F_TINY, f.x + f.w * 0.5f, f.y + f.h * 0.5f + 4 * s + r.FontHeight(F_TINY) * 1.4f,
                         Pal::textFaint, "A pen is measured separately: draw with it to see its pressure.");
    }

    r.ClearClip();
}

static bool Overlaps(const Rect2& a, const Rect2& b)
{
    return a.x < b.r() && b.x < a.r() && a.y < b.b() && b.y < a.b();
}

// Contacts are drawn after every panel so a finger resting on the stats column
// is still visible; the translucent panels alone would dim it too far.
static void DrawContacts(App& app, const Rect2& box)
{
    Renderer& r = app.rend;
    Tracker& t = app.tracker;
    const float s = r.Scale();
    const float fh = r.FontHeight(F_TINY);

    // Rings and slot badges first, so every readout can be placed clear of
    // all of them: fingers sit closer together than a readout is wide.
    float rads[kMaxSlots] = {};
    std::vector<Rect2> taken;
    for (int i = 0; i < kMaxSlots; ++i)
    {
        const Contact& c = t.Slot(i);
        if (!c.active) continue;
        const Color col = SlotColor(i);

        r.FillRect(Rect2{ box.x, std::round(c.y), box.w, 1 }, col.WithA(0.22f));
        r.FillRect(Rect2{ std::round(c.x), box.y, 1, box.h }, col.WithA(0.22f));

        float rad = 26 * s;
        if (c.cw > 0) rad = Clampf(std::max(c.cw, c.ch) * 0.5f, 12 * s, 90 * s);
        rads[i] = rad;
        r.Glow(c.x, c.y, rad * 1.9f, col.WithA(0.20f));
        r.Disc(c.x, c.y, rad * 0.30f, col.WithA(0.98f));
        r.RingShape(c.x, c.y, rad, col.WithA(0.92f));
        if (c.pressure >= 0)
            r.RingShape(c.x, c.y, rad * (0.4f + 0.6f * Clampf(c.pressure, 0.f, 1.f)),
                        col.WithA(0.35f));

        // Slot badge stays wherever the finger is; it is one glyph and is what
        // identifies the contact.
        char id[8]; snprintf(id, sizeof id, "%d", i + 1);
        const float by = c.y - rad - r.FontHeight(F_HEAD) - 4 * s;
        r.TextCenter(F_HEAD, c.x, by, col, "%s", id);
        // With a margin, so a readout does not butt up against another
        // finger and read as that one's.
        const float bw = r.TextW(F_HEAD, id), m = 8 * s;
        taken.push_back(Rect2{ c.x - rad - m, c.y - rad - m, (rad + m) * 2, (rad + m) * 2 });
        taken.push_back(Rect2{ c.x - bw * 0.5f - m * 0.5f, by, bw + m, r.FontHeight(F_HEAD) });
    }

    // Each readout goes beside its finger, on whichever side is clear of the
    // other rings, badges and readouts; with no clear side it is left out, and
    // its figures are still in the per-contact table. A readout is shown only
    // over open canvas, where it cannot sit on top of panel figures.
    for (int i = 0; i < kMaxSlots; ++i)
    {
        const Contact& c = t.Slot(i);
        if (!c.active || !app.freeArea.Contains(c.x, c.y)) continue;
        const float rad = rads[i];

        char l1[48], l2[48], l3[24] = "";
        snprintf(l1, sizeof l1, "%.0f, %.0f px", c.x, c.y);
        if (c.instHz > 0) snprintf(l2, sizeof l2, "%.0f Hz  id %u", c.instHz, c.pointerId);
        else              snprintf(l2, sizeof l2, "id %u", c.pointerId);
        if (c.pressure >= 0) snprintf(l3, sizeof l3, "p %.2f", c.pressure);
        const int lines = l3[0] ? 3 : 2;
        const float lw = std::max({ r.TextW(F_TINY, l1), r.TextW(F_TINY, l2), r.TextW(F_TINY, l3) });
        const float lh = fh * 1.2f * (lines - 1) + fh;

        const Rect2 sides[] = {
            { c.x + rad + 8 * s, c.y - fh, lw, lh },             // right
            { c.x - rad - 8 * s - lw, c.y - fh, lw, lh },        // left
            { c.x - lw * 0.5f, c.y + rad + 10 * s, lw, lh },     // below, past the ring's margin
        };
        const Rect2* at = nullptr;
        for (const Rect2& cand : sides)
        {
            if (cand.x < app.freeArea.x || cand.r() > app.freeArea.r() ||
                cand.y < app.freeArea.y || cand.b() > app.freeArea.b()) continue;
            // Every side keeps clear of its own ring and badge by construction.
            bool clear = true;
            for (const Rect2& o : taken)
                if (Overlaps(cand, o)) { clear = false; break; }
            if (clear) { at = &cand; break; }
        }
        if (!at) continue;
        taken.push_back(*at);

        // In the finger's colour, since a readout below or to the left can
        // land nearer a neighbour than its own finger.
        const float lx = at->x, ly = at->y;
        r.Text(F_TINY, lx, ly, SlotColor(i), l1);
        r.Text(F_TINY, lx, ly + fh * 1.2f, c.instHz > 0 ? RateColor(c.instHz) : Pal::textFaint, l2);
        if (l3[0]) r.Text(F_TINY, lx, ly + fh * 2.4f, Pal::textDim, l3);
    }
}

// The pen tip. A pale ring marks full pressure and a bright one inside it the
// pressure now, so how hard the pen is pressed reads at a glance; a hovering
// pen gets a small marker where it would land.
static void DrawPen(App& app, const Rect2& box)
{
    Renderer& r = app.rend;
    const Tracker& t = app.pen;
    const float s = r.Scale();
    const Color col = Pal::pen;
    const float full = 34 * s;
    const float fh = r.FontHeight(F_TINY);

    for (int i = 0; i < kMaxSlots; ++i)
    {
        const Contact& c = t.Slot(i);
        if (!c.active) continue;
        const float p = c.pressure >= 0 ? Clampf(c.pressure, 0.f, 1.f) : 0.f;

        r.FillRect(Rect2{ box.x, std::round(c.y), box.w, 1 }, col.WithA(0.18f));
        r.FillRect(Rect2{ std::round(c.x), box.y, 1, box.h }, col.WithA(0.18f));
        r.Glow(c.x, c.y, full * 1.5f, Pal::accent.WithA(0.06f + 0.18f * p));
        r.RingShape(c.x, c.y, full, col.WithA(0.30f));
        if (c.pressure >= 0)
            r.RingShape(c.x, c.y, std::max(3.f * s, full * p), Pal::accent.WithA(0.95f));
        r.Disc(c.x, c.y, 2.5f * s, col);

        // Readouts only over open canvas, as for a finger.
        if (!app.freeArea.Contains(c.x, c.y)) continue;
        float lx = c.x + full + 8 * s, ly = c.y - fh * 1.2f;
        if (lx + 190 * s > app.freeArea.r()) lx = c.x - full - 8 * s - 190 * s;
        r.Textf(F_TINY, lx, ly, Pal::text, "%.0f, %.0f px", c.x, c.y);
        ly += fh * 1.2f;
        if (c.pressure >= 0)
            r.Textf(F_TINY, lx, ly, Pal::accent, "pressure %.3f  %ld/1024", c.pressure, std::lround(c.pressure * 1024.0));
        else
            r.Text(F_TINY, lx, ly, Pal::textFaint, "pressure not reported");
        ly += fh * 1.2f;
        char more[64];
        int n = snprintf(more, sizeof more, c.instHz > 0 ? "%.0f Hz" : "-- Hz", c.instHz);
        if (c.tiltX != kNoTilt && c.tiltY != kNoTilt)
            n += snprintf(more + n, sizeof more - n, "  tilt %d/%d", c.tiltX, c.tiltY);
        if (c.penFlags & PEN_FLAG_BARREL) n += snprintf(more + n, sizeof more - n, "  barrel");
        if (c.penFlags & (PEN_FLAG_ERASER | PEN_FLAG_INVERTED)) snprintf(more + n, sizeof more - n, "  eraser");
        r.Text(F_TINY, lx, ly, c.instHz > 0 ? RateColor(c.instHz) : Pal::textFaint, more);
    }

    if (t.Hovering(app.nowQpc))
    {
        const PenHover& h = t.Hover();
        r.RingShape(h.x, h.y, 8 * s, col.WithA(0.55f));
        r.Disc(h.x, h.y, 1.5f * s, col.WithA(0.85f));
        if (app.freeArea.Contains(h.x, h.y))
            r.Text(F_TINY, h.x + 14 * s, h.y - fh * 0.5f, Pal::textFaint,
                   (h.penFlags & PEN_FLAG_BARREL) ? "hover  barrel" : "hover");
    }
}

// Touches the panel reported that Windows did not deliver. These are exactly
// the touches that otherwise leave no trace on screen, so they are drawn over
// everything. Positions come from the panel's own HID coordinates.
static void DrawUndelivered(App& app, const Rect2& box)
{
    Renderer& r = app.rend;
    const Tracker& t = app.tracker;
    const float s = r.Scale();
    const POINT o = t.ClientOrigin();
    const int64_t now = app.nowQpc;

    // Settled ones stay as markers until the ink is cleared.
    const std::vector<UndeliveredTouch>& und = t.Undelivered();
    for (size_t i = t.UndeliveredMarkStart(); i < und.size(); ++i)
    {
        const UndeliveredTouch& u = und[i];
        if (!u.mapped) continue;
        const float x = u.x - (float)o.x, y = u.y - (float)o.y;
        if (!box.Contains(x, y)) continue;
        r.RingShape(x, y, 13 * s, Pal::bad.WithA(0.55f));
        r.Cross(x, y, 7 * s, std::max(1.f, 2 * s), Pal::bad.WithA(0.9f));
    }

    // A live contact becomes suspect only after the normal few-millisecond
    // gap between a raw report and its pointer message has clearly passed,
    // and only if it started in this window rather than on another one.
    const float pulse = 0.55f + 0.45f * (float)std::sin(QpcToSec(now) * 12.0);
    for (const HidTrack& h : t.HidTracks())
    {
        if (h.delivered || h.ended || !h.mapped || h.offWindow || t.PenExcuses(h)) continue;
        if (QpcToMs(now - h.firstQpc) < 60.0) continue;

        const float x = h.sx - (float)o.x, y = h.sy - (float)o.y;
        r.Glow(x, y, 60 * s, Pal::bad.WithA(0.22f * pulse));
        r.RingShape(x, y, 34 * s, Pal::bad.WithA(pulse));
        r.Cross(x, y, 11 * s, std::max(1.f, 2 * s), Pal::bad);

        // The finger is usually at the edge, so put the label towards the centre.
        const char* msg = "panel touch not delivered by Windows";
        const float tw = r.TextW(F_BODY, msg), th = r.FontHeight(F_BODY);
        float lx = (x > box.x + box.w * 0.5f) ? x - 44 * s - tw : x + 44 * s;
        float ly = (y > box.y + box.h * 0.5f) ? y - 44 * s - th : y + 44 * s;
        lx = Clampf(lx, box.x + 6 * s, box.r() - tw - 6 * s);
        ly = Clampf(ly, box.y + 6 * s, box.b() - th - 6 * s);
        r.FillRect(Rect2{ lx - 6 * s, ly - 3 * s, tw + 12 * s, th + 6 * s }, Color(0.05f, 0.02f, 0.03f, 0.88f));
        r.FrameRect(Rect2{ lx - 6 * s, ly - 3 * s, tw + 12 * s, th + 6 * s }, 1.f, Pal::bad);
        r.Text(F_BODY, lx, ly, Pal::bad, msg);
    }
}

// The touch pad's surface, drawn to scale in the open canvas with its contacts
// where they are on the pad. A pad is not mapped to the screen, so this is the
// only place its touches can be shown.
static void DrawPad(App& app, const Rect2& area)
{
    Renderer& r = app.rend;
    const Tracker& t = app.pad;
    const float s = r.Scale();
    const int64_t now = app.nowQpc;

    HidDescriptorInfo d = app.padIn.Desc();
    if (!d.valid)
        if (const TouchDevice* dev = app.ShownDevice())
            if (dev->rawHandle) app.hid.Describe(dev->rawHandle, d);
    const bool haveRange = d.valid && d.xMax > d.xMin && d.yMax > d.yMin;
    const bool haveMm = d.widthMm > 0 && d.heightMm > 0;

    float aspect = 1.6f;   // a typical laptop pad, until the descriptor says otherwise
    if (haveMm) aspect = (float)(d.widthMm / d.heightMm);
    else if (haveRange) aspect = (float)(d.xMax - d.xMin) / (float)(d.yMax - d.yMin);
    aspect = Clampf(aspect, 0.5f, 4.f);

    const float lineH = std::round(r.FontHeight(F_TINY) * 1.7f);
    const float margin = std::round(28 * s);
    const Rect2 room{ area.x + margin, area.y + margin + lineH,
                      area.w - margin * 2, area.h - margin * 2 - lineH * 2 };
    if (room.w < 80 * s || room.h < 50 * s) return;
    float w = room.w, h = w / aspect;
    if (h > room.h) { h = room.h; w = h * aspect; }
    const Rect2 pad{ std::round(room.x + (room.w - w) * 0.5f), std::round(room.y + (room.h - h) * 0.5f),
                     std::round(w), std::round(h) };

    const bool click = app.padIn.ButtonDown();
    r.FillRect(pad, Pal::panel.WithA(0.9f));
    // A 10 mm grid when the pad declares its size, otherwise eighths.
    {
        const int nx = haveMm ? std::max(1, (int)(d.widthMm / 10.0)) : 8;
        const int ny = haveMm ? std::max(1, (int)(d.heightMm / 10.0)) : 8;
        const float sx = haveMm ? pad.w * (float)(10.0 / d.widthMm) : pad.w / 8.f;
        const float sy = haveMm ? pad.h * (float)(10.0 / d.heightMm) : pad.h / 8.f;
        for (int i = 1; i <= nx; ++i)
            if (pad.x + sx * i < pad.r() - 1) r.FillRect(Rect2{ std::round(pad.x + sx * i), pad.y, 1, pad.h }, Pal::grid);
        for (int i = 1; i <= ny; ++i)
            if (pad.y + sy * i < pad.b() - 1) r.FillRect(Rect2{ pad.x, std::round(pad.y + sy * i), pad.w, 1 }, Pal::grid);
    }
    r.FrameRect(pad, click ? std::max(2.f, 2 * s) : 1.f, click ? Pal::accent : Pal::edge);

    char lab[160];
    int n = snprintf(lab, sizeof lab, "TOUCH PAD");
    if (haveMm) n += snprintf(lab + n, sizeof lab - n, "   %.0f x %.0f mm", d.widthMm, d.heightMm);
    if (haveRange) n += snprintf(lab + n, sizeof lab - n, "   %d x %d units", d.xMax - d.xMin, d.yMax - d.yMin);
    const float ly = pad.y - lineH + 3 * s;
    r.Text(F_TINY, pad.x, ly, Pal::accent, lab);
    if (click)
        r.TextRight(F_TINY, pad.r(), ly, Pal::accent, "button pressed");
    else if (d.hasButton)
        r.TextRight(F_TINY, pad.r(), ly, Pal::textFaint, "%llu click%s",
                    (unsigned long long)app.padIn.Clicks(), app.padIn.Clicks() == 1 ? "" : "s");
    r.TextCenter(F_TINY, pad.x + pad.w * 0.5f, pad.b() + 6 * s, Pal::textFaint, "%s",
                 FitText(r, F_TINY, haveMm ? "Drawn to scale from the pad's own reports. Windows never passes "
                                             "a touch pad to apps as touch."
                                           : "Positions from the pad's own reports. Windows never passes "
                                             "a touch pad to apps as touch.", area.w - margin * 2).c_str());

    if (t.TotalFrames() == 0)
    {
        bool screen = false;
        for (const TouchDevice& dv : app.devices)
            if ((dv.usage == 0x04 || dv.maxContacts > 1) && (dv.vid || dv.pid)) screen = true;
        r.TextCenter(F_HEAD, pad.x + pad.w * 0.5f, pad.y + pad.h * 0.5f - r.FontHeight(F_HEAD),
                     Pal::textFaint, "Put fingers on the touch pad");
        r.TextCenter(F_TINY, pad.x + pad.w * 0.5f, pad.y + pad.h * 0.5f + 4 * s, Pal::textFaint, "%s",
                     FitText(r, F_TINY, screen ? "Drag slowly to see sample spacing. Touch the screen to switch to it."
                                               : "Drag slowly to see sample spacing.",
                             pad.w - 20 * s).c_str());
    }

    if (!haveRange) return;
    auto px = [&](float x) { return pad.x + (x - (float)d.xMin) / (float)(d.xMax - d.xMin) * pad.w; };
    auto py = [&](float y) { return pad.y + (y - (float)d.yMin) / (float)(d.yMax - d.yMin) * pad.h; };

    r.SetClip(pad);
    // The dots are the reports: their spacing along a stroke is the sample rate.
    if (app.view.showTrails)
    {
        const double fadeMs = 1400.0;
        for (int i = 0; i < kMaxSlots; ++i)
        {
            const Contact& c = t.Slot(i);
            if (c.trailCount < 2) continue;
            const Color base = SlotColor(i);
            for (size_t k = 1; k < c.trailCount; ++k)
            {
                const TrailPt& a = c.TrailAt(k - 1);
                const TrailPt& b = c.TrailAt(k);
                const double age = QpcToMs(now - b.qpc);
                if (age > fadeMs) continue;
                const float alpha = (float)(1.0 - age / fadeMs);
                r.Line(px(a.x), py(a.y), px(b.x), py(b.y), std::max(1.f, 1.6f * s), base.WithA(alpha * 0.65f));
                if (app.view.showDots) r.Disc(px(b.x), py(b.y), 2.3f * s, base.WithA(alpha * 0.95f));
            }
        }
    }

    // A fingertip is about 10 mm across.
    const float rad = haveMm ? Clampf(pad.w * (float)(5.0 / d.widthMm), 8 * s, 40 * s) : 16 * s;
    for (int i = 0; i < kMaxSlots; ++i)
    {
        const Contact& c = t.Slot(i);
        if (!c.active) continue;
        const Color col = SlotColor(i);
        const float x = px(c.x), y = py(c.y);
        r.Glow(x, y, rad * 1.9f, col.WithA(0.20f));
        r.Disc(x, y, rad * 0.30f, col.WithA(0.98f));
        r.RingShape(x, y, rad, col.WithA(0.92f));
        r.TextCenter(F_BODY, x, y - rad - r.FontHeight(F_BODY) - 3 * s, col, "%d", i + 1);
    }
    r.ClearClip();
}

// ================================================================ stats panel

static void DrawStats(App& app, const Rect2& box)
{
    Renderer& r = app.rend;
    const Tracker& t = app.Shown();
    const bool pad = app.ShowingPad();
    const bool pen = app.ShowingPen();
    const TouchPadInput& in = app.padIn;
    const float s = r.Scale();
    const int64_t now = app.nowQpc;

    r.FillRect(box, Pal::panel2.WithA(kColumnA));
    r.FillRect(Rect2{ box.x, box.y, 1, box.h }, Pal::edge);
    // Last line of defence: the layout sizes the column to fit, but if the
    // screen is too small even for that, content is cut rather than drawn over
    // the neighbouring panels.
    r.SetClip(box);

    StatsMetrics m = MeasureStats(r, false);
    if (box.h < m.Need()) m = MeasureStats(r, true);
    const float rowH = m.rowH, titleH = m.titleH, gap = m.gap, pipR = m.pipR;
    const float hTiming = m.hTiming, hDeliver = m.hDeliver, hDisplay = m.hDisplay, hMulti = m.hMulti;
    const float chrome = m.chrome;
    const float bw = box.w - m.pad * 2;
    const float bx = box.x + m.pad;

    const double liveHz = t.FrameHz(now);
    const double rateHz = t.RateHz();
    const Stats& iv = t.IntervalMs();
    const double periodMs = rateHz > 0 ? 1000.0 / rateHz : 0.0;

    // Give the hero block whatever is left after the fixed-size blocks, so a
    // short window shrinks the big number instead of clipping the last panel.
    float heroH = box.h - m.fixedH - m.hCounters;
    bool roomForCounters = heroH >= m.heroMin;
    if (!roomForCounters) heroH = box.h - m.fixedH;        // drop the counters first
    heroH = Clampf(heroH, m.heroMin, m.heroMax);
    (void)chrome;

    float y = box.y + m.pad;

    // ---- hero: report rate
    {
        Rect2 hb{ bx, y, bw, heroH };
        Block(r, hb, pad ? "TOUCH PAD REPORT RATE  (HID frames per second)"
                   : pen ? "PEN REPORT RATE  (input frames per second, tip down)"
                         : "TOUCH SCREEN REPORT RATE  (input frames per second)", kBlockA);

        // Use the big face only when the block is tall enough to hold it.
        const int face = (heroH >= r.FontHeight(F_HERO) + chrome) ? F_HERO : F_HEAD;
        Color c = RateColor(liveHz > 0 ? liveHz : rateHz);
        char num[32];
        if (liveHz > 0)       snprintf(num, sizeof num, "%.0f", liveHz);
        else if (rateHz > 0)  snprintf(num, sizeof num, "%.0f", rateHz);
        else                  snprintf(num, sizeof num, "--");

        const float ny = hb.y + r.FontHeight(F_TINY) * 1.6f;
        r.Text(face, hb.x + 10 * s, ny, c, num);
        const float nw = r.TextW(face, num);
        r.Text(F_BODY, hb.x + 10 * s + nw + 8 * s,
               ny + r.FontHeight(face) - r.FontHeight(F_BODY) - 3 * s, Pal::textDim, "Hz");

        // The session's report rate, and whether the gap between reports
        // varies, stacked at the right, centred against the big number - but
        // never above it, where the pair would run into the block title.
        const float rx = hb.r() - 12 * s;
        const float pairH = r.FontHeight(F_BODY) * 1.25f;
        float ry = ny + std::max(0.f, (r.FontHeight(face) - pairH * 2) * 0.5f);
        r.TextRight(F_BODY, rx, ry, rateHz > 0 ? Pal::text : Pal::textFaint,
                    rateHz > 0 ? "session  %.1f Hz" : "session  -- Hz", rateHz);
        if (t.CoarseClock())
            r.TextRight(F_BODY, rx, ry + pairH, Pal::textDim, "gaps  %g ms clock", t.ClockResolutionMs());
        else if (!t.GapsJudged())
            r.TextRight(F_BODY, rx, ry + pairH, Pal::textFaint, "gaps  --");
        else
            r.TextRight(F_BODY, rx, ry + pairH, Pal::textDim, t.GapsVary() ? "gaps  vary" : "gaps  steady");
        y = hb.b() + gap;
    }

    // ---- rate detail
    {
        Rect2 b{ bx, y, bw, hTiming };
        Block(r, b, "REPORT TIMING", kBlockA);
        Col c(r, Rect2{ b.x + 10 * s, b.y + titleH, bw - 20 * s, 0 });
        c.lh = rowH;
        if (iv.n)
        {
            c.Row("interval  mean", Pal::text, "%.3f ms", iv.mean);
            c.Row("jitter  sd", JitterColor(iv.Sd(), periodMs), "%.3f ms", iv.Sd());
            c.Row("interval  min / max", Pal::textDim, "%.2f / %.2f ms", iv.mn, iv.mx);
            c.Row("p99 / p99.9", Pal::textDim, "%.2f / %.2f ms",
                  t.IntervalHist().Percentile(0.99), t.IntervalHist().Percentile(0.999));
            c.Row("worst gap", t.MaxGapMs() > 25 ? Pal::bad
                             : (t.MaxGapMs() > periodMs * 3 && periodMs > 0 ? Pal::warn : Pal::textDim),
                  "%.2f ms", t.MaxGapMs());
        }
        else
        {
            c.Row("interval  mean", Pal::textFaint, "--");
            c.Row("jitter  sd", Pal::textFaint, "--");
            c.Row("interval  min / max", Pal::textFaint, "--");
            c.Row("p99 / p99.9", Pal::textFaint, "--");
            c.Row("worst gap", Pal::textFaint, "--");
        }
        double hidHz = t.HidReportHz(now);
        // A panel that splits a scan sends more reports than it makes scans,
        // so the label says which of the two this rate counts.
        const char* hidLabel = t.HidSplitReports() ? "raw HID reports (split)" : "raw HID reports";
        // Raw HID and delivery share a row: a lost touch is the thing to see.
        // A pad has no delivery to check; which clock timed it matters instead.
        if (pad)
        {
            if (!in.Seen())            c.Row("timed by", Pal::textFaint, "--");
            else if (in.ScanRejected()) c.Row("timed by", Pal::warn, "arrival (pad clock rejected)");
            else if (in.ScanClock() && in.ClockStepMs() >= 0.5)
                c.Row("timed by", Pal::accent, "pad clock, %g ms steps", in.ClockStepMs());
            else if (in.ScanClock())   c.Row("timed by", Pal::accent, "pad clock, 100 us");
            else                       c.Row("timed by", Pal::textDim, "arrival (no pad clock)");
        }
        // A pen reports while it hovers too. Its raw reports count both, and
        // the hover rate is timed from the hover reports Windows delivers.
        else if (pen && t.HidSeen())
            c.Row(hidLabel, Pal::accent, "%.0f /s   hover %.0f /s", hidHz, t.HoverLiveHz(now));
        else if (pen)
            c.Row("hover reports", t.HoverLiveHz(now) > 0 ? Pal::accent : Pal::textFaint,
                  t.HoverReports() ? "%.0f /s" : "--", t.HoverLiveHz(now));
        else if (!t.HidSeen())
            c.Row(hidLabel, Pal::textFaint, "not observed");
        else if (t.HidUndelivered())
            c.Row(hidLabel, Pal::bad, "%.0f /s   %llu touch%s lost", hidHz,
                  (unsigned long long)t.HidUndelivered(), t.HidUndelivered() == 1 ? "" : "es");
        else if (t.HidContacts())
            c.Row(hidLabel, Pal::accent, "%.0f /s   none lost", hidHz);
        else
            c.Row(hidLabel, Pal::accent, "%.0f /s", hidHz);
        y = b.b() + gap;
    }

    // ---- touch pad input, in place of delivery
    if (pad)
    {
        Rect2 b{ bx, y, bw, hDeliver };
        Block(r, b, "TOUCH PAD INPUT  (its own HID reports)", kBlockA);
        Col c(r, Rect2{ b.x + 10 * s, b.y + titleH, bw - 20 * s, 0 });
        c.lh = rowH;
        const HidDescriptorInfo& d = in.Desc();
        const Stats& aj = in.AddedJitterMs();
        if (in.ScanClock() && aj.n > 1)
            c.Row("added jitter  sd", JitterColor(aj.Sd(), periodMs), "%.3f ms  pad -> app", aj.Sd());
        else
            c.Row("added jitter  sd", Pal::textFaint, "--");
        // Reports held up on the way and handed over two at once.
        if (in.ArrivalSteps())
        {
            const double pct = 100.0 * (double)in.Bunched() / (double)in.ArrivalSteps();
            c.Row("arrived bunched", pct >= 5.0 ? Pal::warn : Pal::textDim, "%.1f%%   worst wait %.0f ms",
                  pct, in.ArrivalIntervalMs().mx);
        }
        else
            c.Row("arrived bunched", Pal::textFaint, "--");
        c.Row("reports / frames", Pal::textDim, "%.0f /s  %.0f /s%s", in.ReportHz(now), t.FrameHz(now),
              in.Hybrid() ? "  split" : "");
        c.Row("palms rejected", in.Palms() ? Pal::text : Pal::textDim, "%llu", (unsigned long long)in.Palms());
        if (!in.Seen() || !d.hasButton)
            c.Row("button", Pal::textFaint, "--");
        else
            c.Row("button", in.ButtonDown() ? Pal::accent : Pal::textDim, "%s   %llu click%s",
                  in.ButtonDown() ? "pressed" : "up", (unsigned long long)in.Clicks(),
                  in.Clicks() == 1 ? "" : "s");
        y = b.b() + gap;
    }
    else
    {
        Rect2 b{ bx, y, bw, hDeliver };
        Block(r, b, "DELIVERY  (device timestamp -> app)", kBlockA);
        Col c(r, Rect2{ b.x + 10 * s, b.y + titleH, bw - 20 * s, 0 });
        c.lh = rowH;
        const Stats& la = t.LatencyMs();
        if (la.n)
        {
            c.Row("latency  mean", la.mean > 12 ? Pal::warn : Pal::good, "%.2f ms", la.mean);
            c.Row("latency  p99", Pal::textDim, "%.2f ms", t.LatencyHist().Percentile(0.99));
            c.Row("latency  min / max", Pal::textDim, "%.2f / %.2f ms", la.mn, la.mx);
        }
        else
        {
            c.Row("latency  mean", Pal::textFaint, "--");
            c.Row("latency  p99", Pal::textFaint, "--");
            c.Row("latency  min / max", Pal::textFaint, "--");
        }
        c.Row("messages / samples", Pal::textDim, "%.0f /s  %.0f /s",
              t.MessageHz(now), t.SampleHz(now));
        const Stats& pp = t.PredictPx();
        c.Row("OS position shift", pp.n && pp.mean > 0.5 ? Pal::warn : Pal::textDim,
              pp.n ? "%.2f px avg" : "--", pp.mean);
        y = b.b() + gap;
    }

    // ---- display / frame
    {
        Rect2 b{ bx, y, bw, hDisplay };
        Block(r, b, "DISPLAY & FRAME RATE", kBlockA);
        Col c(r, Rect2{ b.x + 10 * s, b.y + titleH, bw - 20 * s, 0 });
        c.lh = rowH;

        c.Row("render", app.frame.windowHz > 0 ? Pal::good : Pal::textFaint,
              "%.0f fps   %.2f ms", app.frame.windowHz, app.frame.curMs);
        c.Row("frame  mean / p99", Pal::textDim, "%.2f / %.2f ms",
              app.frame.AvgMs(), app.frame.P99Ms());
        c.Row("1% low", Pal::textDim, "%.0f fps", app.frame.Low1Fps());

        if (app.vblank.Valid())
            c.Row("measured refresh", Pal::accent, "%.3f Hz  +/-%.3f ms",
                  app.vblank.Hz(), app.vblank.JitterMs());
        else if (app.monitor.dwmValid)
            c.Row("measured refresh", Pal::textDim, "%.3f Hz (DWM, desktop)", app.monitor.dwmHz);
        else
            c.Row("measured refresh", Pal::textFaint, "--");

        c.Row("present", app.view.vsync ? Pal::warn : Pal::good, "%s",
              app.view.vsync ? "vsync"
                             : (app.rend.TearingSupported() ? "immediate + tearing" : "immediate"));
        y = b.b() + gap;
    }

    // ---- the pen's pressure, tilt and buttons, in place of the contact test
    if (pen)
    {
        Rect2 b{ bx, y, bw, hMulti };
        Block(r, b, "PEN  (pressure as Windows passes it on, 0-1024)", kBlockA);
        Col c(r, Rect2{ b.x + 10 * s, b.y + titleH, bw - 20 * s, 0 });
        c.lh = rowH;

        const Contact* tip = nullptr;
        for (int i = 0; i < kMaxSlots && !tip; ++i) if (t.Slot(i).active) tip = &t.Slot(i);
        const bool hovering = t.Hovering(now);
        const PenHover& hv = t.Hover();
        const float pNow = tip && tip->pressure >= 0 ? Clampf(tip->pressure, 0.f, 1.f) : -1.f;
        const Stats& pr = t.Pressure();
        const Stats& pd = t.PressureAtDown();

        if (pNow >= 0)     c.Row("pressure", Pal::accent, "%.3f   %ld of 1024", pNow, std::lround(pNow * 1024.0));
        else if (tip)      c.Row("pressure", Pal::textDim, "not reported");
        else if (hovering) c.Row("pressure", Pal::textDim, "0   hovering");
        else               c.Row("pressure", Pal::textFaint, "--");
        if (pr.n) c.Row("at pen-down / peak", Pal::textDim, "%.3f / %.3f", pd.n ? pd.mean : 0.0, pr.mx);
        else      c.Row("at pen-down / peak", Pal::textFaint, "--");

        // The gauge sits where the contact pips would: pressure now, quarter
        // marks, and the session's peak.
        const float gx = b.x + 12 * s, gw = bw - 24 * s;
        const float gh = std::round(pipR * 1.2f);
        const float gy = std::round(c.y + pipR + 4 * s - gh * 0.5f);
        r.FillRect(Rect2{ gx, gy, gw, gh }, Pal::bg.WithA(0.6f));
        if (pNow > 0) r.FillRect(Rect2{ gx, gy, std::max(1.f, gw * pNow), gh }, Pal::accent.WithA(0.85f));
        for (int k = 1; k < 4; ++k)
            r.FillRect(Rect2{ std::round(gx + gw * k / 4.f), gy, 1, gh }, Pal::edge);
        r.FrameRect(Rect2{ gx, gy, gw, gh }, 1.f, Pal::edge);
        if (pr.n)
            r.FillRect(Rect2{ std::round(gx + gw * Clampf((float)pr.mx, 0.f, 1.f)) - 1, gy - 3 * s,
                              std::max(2.f, 2 * s), gh + 6 * s }, Pal::text);

        // Below it: tilt now, the buttons lit while held, and how many of
        // Windows' pressure levels the session has used.
        const float ty = gy + gh + 5 * s;
        float tx = gx;
        const int8_t tiltX = tip ? tip->tiltX : hovering ? hv.tiltX : kNoTilt;
        const int8_t tiltY = tip ? tip->tiltY : hovering ? hv.tiltY : kNoTilt;
        const uint8_t flags = tip ? tip->penFlags : hovering ? hv.penFlags : 0;
        char tb[48];
        if (tiltX != kNoTilt && tiltY != kNoTilt) snprintf(tb, sizeof tb, "tilt %d / %d deg", tiltX, tiltY);
        else snprintf(tb, sizeof tb, t.TiltX().n ? "tilt --" : "tilt not reported");
        r.Text(F_TINY, tx, ty, t.TiltX().n ? Pal::textDim : Pal::textFaint, tb);
        tx += r.TextW(F_TINY, tb) + 16 * s;
        r.Text(F_TINY, tx, ty, (flags & PEN_FLAG_BARREL) ? Pal::accent : Pal::textFaint, "barrel");
        tx += r.TextW(F_TINY, "barrel") + 12 * s;
        r.Text(F_TINY, tx, ty, (flags & (PEN_FLAG_ERASER | PEN_FLAG_INVERTED)) ? Pal::accent : Pal::textFaint, "eraser");
        tx += r.TextW(F_TINY, "eraser") + 16 * s;
        char lv[32];
        snprintf(lv, sizeof lv, "%u levels seen", t.PressureLevels());
        if (tx + r.TextW(F_TINY, lv) <= gx + gw)
            r.TextRight(F_TINY, gx + gw, ty, t.PressureLevels() ? Pal::textDim : Pal::textFaint, "%s", lv);
        y = b.b() + gap;
    }
    else   // ---- contacts / ten finger test
    {
        Rect2 b{ bx, y, bw, hMulti };
        Block(r, b, pad ? "MULTI-TOUCH  (touch pad, Hz per count)"
                        : "MULTI-TOUCH  (10 finger test, Hz per count)", kBlockA);
        Col c(r, Rect2{ b.x + 10 * s, b.y + titleH, bw - 20 * s, 0 });
        c.lh = rowH;

        // What the device declares it can track, not SM_MAXIMUMTOUCHES. A pad
        // that splits frames across reports may hold more than one report
        // carries, and it declares no maximum of its own that Windows exposes.
        int devMax = 0;
        if (const TouchDevice* dev = app.ShownDevice())
            devMax = (int)dev->ContactCapacity();
        if (pad && in.Hybrid()) devMax = 0;
        // Ten pips for a screen; a pad gets as many as it can report, at least five.
        const int pips = pad ? std::min(10, std::max({ 5, devMax, t.MaxSimultaneous() })) : 10;

        c.Row("contacts now", t.ContactsNow() ? Pal::good : Pal::textFaint,
              "%d", t.ContactsNow());
        Color mc = (devMax && t.MaxSimultaneous() >= devMax) ? Pal::good
                 : (t.MaxSimultaneous() >= pips ? Pal::good : Pal::warn);
        if (devMax) c.Row("max reached / device", mc, "%d  /  %d", t.MaxSimultaneous(), devMax);
        else        c.Row("max reached", mc, "%d", t.MaxSimultaneous());

        float px = b.x + 12 * s + pipR;
        float py = c.y + pipR + 4 * s;
        const float pipStep = (bw - 24 * s - pipR * 2) / (float)(pips - 1);
        // Rate measured at each contact count, so a panel that slows down as
        // fingers are added shows it here rather than only in the report.
        const float hzY = py + pipR + 4 * s;
        double bestHz = 0;
        for (int i = 1; i <= pips; ++i)
            if (t.HasDataAt(i)) bestHz = std::max(bestHz, t.RateHzAt(i));

        for (int i = 1; i <= pips; ++i)
        {
            bool hit = t.SawSimultaneous(i);
            float cxp = px + pipStep * (i - 1);

            if (t.HasDataAt(i))
            {
                double hz = t.RateHzAt(i);
                // Amber once this count runs materially slower than the best.
                Color hc = (bestHz > 0 && hz < bestHz * 0.8) ? Pal::warn : Pal::textDim;
                r.TextCenter(F_TINY, cxp, hzY, hc, "%.0f", hz);
            }
            else
            {
                r.TextCenter(F_TINY, cxp, hzY, Pal::textFaint, "-");
            }

            if (hit)
            {
                r.Disc(cxp, py, pipR, SlotColor(i - 1).WithA(0.9f));
                r.TextCenter(F_TINY, cxp, py - r.FontHeight(F_TINY) * 0.5f, Pal::bg, "%d", i);
            }
            else
            {
                r.RingShape(cxp, py, pipR, Pal::edge);
                r.TextCenter(F_TINY, cxp, py - r.FontHeight(F_TINY) * 0.5f, Pal::textFaint, "%d", i);
            }
        }
        y = b.b() + gap;
    }

    // ---- session counters
    if (roomForCounters && y + r.FontHeight(F_TINY) * 2 < box.b())
    {
        Col c(r, Rect2{ bx + 10 * s, y + 4 * s, bw - 20 * s, 0 }, 0.52f);
        c.lh = std::round(r.FontHeight(F_TINY) * 1.25f);
        char buf[160];
        if (pad)
            snprintf(buf, sizeof buf, "%llu frames   %llu samples   %llu HID reports",
                     (unsigned long long)t.TotalFrames(), (unsigned long long)t.TotalSamples(),
                     (unsigned long long)in.Reports());
        else
            snprintf(buf, sizeof buf, "%llu frames   %llu samples   %llu recovered",
                     (unsigned long long)t.TotalFrames(), (unsigned long long)t.TotalSamples(),
                     (unsigned long long)t.HistorySamples());
        r.Text(F_TINY, c.x, c.y, Pal::textFaint, buf);
        c.y += c.lh;
        snprintf(buf, sizeof buf, "buffered for export: %zu / %zu rows%s",
                 t.RecordCount(), t.RecordCapacity(),
                 t.RecordsDropped() ? "  (ring wrapped)" : "");
        r.Text(F_TINY, c.x, c.y, Pal::textFaint, buf);
    }

    if (pad && in.ScanRejected())
        r.TextRight(F_TINY, box.r() - m.pad, box.b() - r.FontHeight(F_TINY) - 2 * s, Pal::warn,
                    "pad clock disagreed with the host - timed by arrival");
    else if (!pad && !t.UseHistory())
        r.TextRight(F_TINY, box.r() - m.pad, box.b() - r.FontHeight(F_TINY) - 2 * s, Pal::warn,
                    "history recovery OFF - showing delivered rate");
    else if (t.LostContacts())
        r.TextRight(F_TINY, box.r() - m.pad, box.b() - r.FontHeight(F_TINY) - 2 * s, Pal::bad,
                    "%llu contact%s lost its up message",
                    (unsigned long long)t.LostContacts(), t.LostContacts() == 1 ? "" : "s");

    r.ClearClip();
}

// ============================================== interval histogram

static void DrawHistogram(App& app, const Rect2& box)
{
    Renderer& r = app.rend;
    const Histogram& h = app.Shown().IntervalHist();
    const float s = r.Scale();

    Block(r, box, "REPORT INTERVAL DISTRIBUTION   (count on a sqrt scale)");

    Rect2 plot{ box.x + 44 * s, box.y + 26 * s,
                box.w - 56 * s, box.h - 26 * s - 26 * s };
    if (plot.w < 20 || plot.h < 20) return;

    if (!h.total)
    {
        r.TextCenter(F_TINY, box.x + box.w * 0.5f, box.y + box.h * 0.5f, Pal::textFaint,
                     "no intervals yet");
        return;
    }

    // Zoom to the populated range so the resolution is not wasted.
    int lo = -1, hi = -1;
    for (int i = 0; i < h.nbins; ++i) if (h.bins[(size_t)i]) { if (lo < 0) lo = i; hi = i; }
    if (lo < 0) return;
    int pad = std::max(2, (hi - lo) / 8);
    lo = std::max(0, lo - pad);
    hi = std::min(h.nbins - 1, hi + pad);
    const int span = hi - lo + 1;

    const double maxC = (double)h.MaxBin();
    const double maxR = std::sqrt(maxC);

    r.FillRect(Rect2{ plot.x, plot.b(), plot.w, 1 }, Pal::edge);

    const float bw = plot.w / (float)span;
    const int modeBin = h.ModeBin();
    for (int i = lo; i <= hi; ++i)
    {
        uint64_t cnt = h.bins[(size_t)i];
        if (!cnt) continue;
        float frac = (float)(std::sqrt((double)cnt) / maxR);
        float bh = std::max(1.f, frac * plot.h);
        float x0 = plot.x + (i - lo) * bw;
        Color c = (i == modeBin) ? Pal::accent : Pal::accent.WithA(0.42f);
        r.FillRect(Rect2{ x0, plot.b() - bh, std::max(1.f, bw - 1), bh }, c);
    }

    // x axis labels in ms, with the Hz equivalent
    auto xOf = [&](double ms) { return plot.x + (float)((ms - (h.lo + lo * h.binW)) / (span * h.binW)) * plot.w; };
    const double msLo = h.lo + lo * h.binW, msHi = h.lo + (hi + 1) * h.binW;
    int ticks = std::max(2, std::min(8, (int)(plot.w / (70 * s))));
    for (int k = 0; k <= ticks; ++k)
    {
        double ms = msLo + (msHi - msLo) * k / ticks;
        float x = xOf(ms);
        r.FillRect(Rect2{ std::round(x), plot.b(), 1, 4 * s }, Pal::edge);
        // The last label ends at the block's edge rather than centring past it.
        char lab[24];
        snprintf(lab, sizeof lab, "%.2f", ms);
        const float lw = r.TextW(F_TINY, lab);
        r.Text(F_TINY, std::min(x - lw * 0.5f, box.r() - 6 * s - lw), plot.b() + 6 * s, Pal::textFaint, lab);
    }
    // The unit sits in the gutter on the tick row; a row of its own below
    // would run past the block.
    r.Text(F_TINY, box.x + 10 * s, plot.b() + 6 * s, Pal::textFaint, "ms");

    // annotate the report rate
    if (modeBin >= lo && modeBin <= hi)
    {
        // The session's report rate, an average: where the gap varies it sits
        // between the bars rather than on the tallest.
        const double rateHz = app.Shown().RateHz();
        const double ms = rateHz > 0 ? 1000.0 / rateHz : h.BinCenter(modeBin);
        float x = xOf(ms);
        r.FillRect(Rect2{ std::round(x), plot.y, 1, plot.h }, Pal::accent.WithA(0.5f));
        char lab[64];
        snprintf(lab, sizeof lab, "%.2f ms = %.1f Hz", ms, ms > 0 ? 1000.0 / ms : 0.0);
        float lw = r.TextW(F_TINY, lab);
        float lx = Clampf(x - lw * 0.5f, plot.x, plot.r() - lw);
        // Backed, because it lands on a bar itself when every interval falls
        // in one bin - which a pad timed on its own clock often does.
        r.FillRect(Rect2{ lx - 3 * s, plot.y - 2 * s, lw + 6 * s, r.FontHeight(F_TINY) + 2 * s },
                   Pal::panel.WithA(0.9f));
        r.Text(F_TINY, lx, plot.y - 1 * s, Pal::accent, lab);
    }

    // y axis: count scale
    r.TextRight(F_TINY, plot.x - 6 * s, plot.y, Pal::textFaint, "%llu",
                (unsigned long long)h.MaxBin());
    r.TextRight(F_TINY, plot.x - 6 * s, plot.b() - r.FontHeight(F_TINY), Pal::textFaint, "0");
}

// ============================================== rate over time

static void DrawRateGraph(App& app, const Rect2& box)
{
    Renderer& r = app.rend;
    const Ring& ring = app.Shown().HzRing();
    const float s = r.Scale();

    Block(r, box, "INSTANTANEOUS RATE OVER TIME   (per report, newest at right)");

    Rect2 plot{ box.x + 48 * s, box.y + 26 * s, box.w - 60 * s, box.h - 26 * s - 20 * s };
    if (plot.w < 20 || plot.h < 20) return;

    if (ring.count < 2)
    {
        r.TextCenter(F_TINY, box.x + box.w * 0.5f, box.y + box.h * 0.5f, Pal::textFaint,
                     "no reports yet");
        return;
    }

    float maxHz = 1.f;
    for (size_t i = 0; i < ring.count; ++i) maxHz = std::max(maxHz, ring.At(i));
    float top = maxHz * 1.08f;

    // gridlines, and one at the session's report rate
    const double rateHz = app.Shown().RateHz();
    for (int k = 1; k <= 4; ++k)
    {
        float v = top * k / 4.f;
        float gy = plot.b() - (v / top) * plot.h;
        r.FillRect(Rect2{ plot.x, std::round(gy), plot.w, 1 }, Pal::grid);
        r.TextRight(F_TINY, plot.x - 6 * s, gy - r.FontHeight(F_TINY) * 0.5f, Pal::textFaint,
                    "%.0f", v);
    }
    if (rateHz > 0 && rateHz < top)
    {
        float gy = plot.b() - (float)(rateHz / top) * plot.h;
        r.FillRect(Rect2{ plot.x, std::round(gy), plot.w, 1 }, Pal::accent.WithA(0.45f));
    }

    // One column per pixel; draw the min/max envelope so dropouts stay visible.
    const int cols = std::max(1, (int)plot.w);
    const size_t n = ring.count;
    for (int c = 0; c < cols; ++c)
    {
        size_t i0 = (size_t)((double)c / cols * n);
        size_t i1 = (size_t)((double)(c + 1) / cols * n);
        if (i1 <= i0) i1 = i0 + 1;
        if (i1 > n) i1 = n;
        if (i0 >= n) break;
        float mn = 1e9f, mx = -1e9f;
        for (size_t i = i0; i < i1; ++i) { float v = ring.At(i); mn = std::min(mn, v); mx = std::max(mx, v); }
        float y0 = plot.b() - Clampf(mx / top, 0.f, 1.f) * plot.h;
        float y1 = plot.b() - Clampf(mn / top, 0.f, 1.f) * plot.h;
        Color col = RateColor(mn).WithA(0.85f);
        r.FillRect(Rect2{ plot.x + c, y0, 1, std::max(1.f, y1 - y0) }, col);
    }

    r.FillRect(Rect2{ plot.x, plot.b(), plot.w, 1 }, Pal::edge);
    r.Text(F_TINY, plot.x, plot.b() + 4 * s, Pal::textFaint, "Hz");
    r.TextRight(F_TINY, plot.r(), plot.b() + 4 * s, Pal::textFaint,
                "%zu reports shown", ring.count);
}

// ============================================== pen pressure over time

// The pressure of each report, newest at right, broken at every lift - the
// pen's counterpart of the contact table, which for one pen has one row.
static void DrawPressure(App& app, const Rect2& box)
{
    Renderer& r = app.rend;
    const Tracker& t = app.pen;
    const Ring& ring = t.PressureRing();
    const float s = r.Scale();

    Block(r, box, "PEN PRESSURE   (per report, newest at right)");

    Rect2 plot{ box.x + 40 * s, box.y + 26 * s, box.w - 52 * s, box.h - 26 * s - 20 * s };
    if (plot.w < 20 || plot.h < 20) return;

    auto yOf = [&](float p) { return plot.b() - Clampf(p, 0.f, 1.f) * plot.h; };
    for (int k = 0; k <= 4; ++k)
    {
        const float gy = std::round(yOf(k / 4.f));
        if (k) r.FillRect(Rect2{ plot.x, gy, plot.w, 1 }, Pal::grid);
        r.TextRight(F_TINY, plot.x - 6 * s, gy - r.FontHeight(F_TINY) * 0.5f, Pal::textFaint,
                    "%.2f", k / 4.0);
    }
    r.FillRect(Rect2{ plot.x, plot.b(), plot.w, 1 }, Pal::edge);

    size_t points = 0;
    for (size_t i = 0; i < ring.count; ++i) if (ring.At(i) >= 0) ++points;
    if (points < 2)
    {
        r.TextCenter(F_TINY, plot.x + plot.w * 0.5f, plot.y + plot.h * 0.5f - r.FontHeight(F_TINY),
                     Pal::textFaint, "no pressure yet - draw with the pen");
        return;
    }

    // One column per pixel, as the rate graph: the spread of the reports in
    // it, and nothing where the pen was lifted.
    const int cols = std::max(1, (int)plot.w);
    const size_t n = ring.count;
    const float thick = std::max(1.f, std::round(1.5f * s));
    for (int c = 0; c < cols; ++c)
    {
        size_t i0 = (size_t)((double)c / cols * n);
        size_t i1 = (size_t)((double)(c + 1) / cols * n);
        if (i1 <= i0) i1 = i0 + 1;
        if (i1 > n) i1 = n;
        if (i0 >= n) break;
        float mn = 2.f, mx = -1.f;
        for (size_t i = i0; i < i1; ++i)
        {
            const float v = ring.At(i);
            if (v < 0) continue;
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        if (mx < 0) continue;
        const float y0 = yOf(mx), y1 = yOf(mn);
        r.FillRect(Rect2{ plot.x + c, y0 - thick * 0.5f, 1, std::max(thick, y1 - y0 + thick) },
                   Pal::accent.WithA(0.9f));
    }

    // The scale note gives way first when the strip is narrow.
    char right[64];
    snprintf(right, sizeof right, "peak %.3f   %zu reports shown", t.Pressure().mx, points);
    const char* note = "0-1024 scaled to 0-1";
    if (plot.x + r.TextW(F_TINY, note) + 16 * s + r.TextW(F_TINY, right) <= plot.r())
        r.Text(F_TINY, plot.x, plot.b() + 4 * s, Pal::textFaint, note);
    r.TextRight(F_TINY, plot.r(), plot.b() + 4 * s, Pal::textFaint, "%s", right);
}

// ============================================== per contact table

static void DrawTable(App& app, const Rect2& box)
{
    Renderer& r = app.rend;
    const Tracker& t = app.Shown();
    const float s = r.Scale();

    Block(r, box, app.ShowingPad() ? "PER-CONTACT DETAIL  (touch pad, pad units)" : "PER-CONTACT DETAIL");

    // Live contacts first, then the most recent finished ones.
    int order[kMaxSlots];
    int total = 0, live = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < kMaxSlots; ++i)
        {
            const Contact& c = t.Slot(i);
            if (!c.everUsed) continue;
            if ((pass == 0) != c.active) continue;
            order[total++] = i;
            if (c.active) ++live;
        }

    const float fh = r.FontHeight(F_TINY);
    const float lhNormal = TableLine(r);
    const float lhTight = std::round(fh);          // Consolas carries its own leading
    const float top = box.y + kTableTop * s;       // below the block title
    const float bottom = box.b() - kTableBottom * s;
    const float x0 = box.x + 10 * s;
    const float w = box.w - 20 * s;
    const float footerH = lhNormal * 2.4f + 4 * s;
    // The small tolerance keeps float rounding at odd DPI scales from losing a
    // row the layout sized the strip to hold.
    auto rowsFit = [&](float lh, float reserve) {
        return (int)((bottom - reserve - top - lh) / lh + 0.01f);
    };

    // Every live contact has to be on screen - that is the ten-finger test.
    // Tighten the rows first, then add as many columns as the live contacts
    // need, dropping fields as the columns narrow.
    const float gutter = 16 * s;
    const float minColW = 56 * s;                  // slot number and Hz
    const int maxCols = std::max(1, (int)((w + gutter) / (minColW + gutter)));

    float lh = lhNormal;
    int ncol = 1;
    if (live > rowsFit(lhNormal, 0))
    {
        lh = lhTight;
        const int per = std::max(1, rowsFit(lhTight, 0));
        ncol = std::min(maxCols, (live + per - 1) / per);
    }
    const bool showFooter = ncol == 1 && lh == lhNormal && total <= rowsFit(lhNormal, footerH);
    const int perCol = std::max(1, rowsFit(lh, showFooter ? footerH : 0));
    const int shown = std::min(total, perCol * ncol);

    const float cw = (w - gutter * (ncol - 1)) / ncol;
    struct Field { const char* h; float f; int kind; };
    enum { F_ID, F_X, F_Y, F_HZ, F_SD, F_N };
    std::vector<Field> fields;
    if (cw >= 330 * s)
        fields = { {"id", 0.10f, F_ID}, {"x", 0.28f, F_X}, {"y", 0.43f, F_Y},
                   {"Hz", 0.60f, F_HZ}, {"sd ms", 0.75f, F_SD}, {"n", 0.90f, F_N} };
    else if (cw >= 170 * s)
        fields = { {"x", 0.20f, F_X}, {"y", 0.42f, F_Y}, {"Hz", 0.64f, F_HZ}, {"sd ms", 0.82f, F_SD} };
    else if (cw >= 110 * s)
        fields = { {"x", 0.24f, F_X}, {"y", 0.50f, F_Y}, {"Hz", 0.76f, F_HZ} };
    else
        fields = { {"Hz", 0.50f, F_HZ} };

    for (int col = 0; col < ncol; ++col)
    {
        const float cx = x0 + col * (cw + gutter);
        r.Text(F_TINY, cx, top, Pal::textFaint, "#");
        for (const Field& fd : fields) r.Text(F_TINY, cx + cw * fd.f, top, Pal::textFaint, fd.h);
        r.FillRect(Rect2{ cx, top + lh - 2 * s, cw, 1 }, Pal::edge);
    }

    for (int k = 0; k < shown; ++k)
    {
        const int i = order[k];
        const Contact& c = t.Slot(i);
        const int col = k / perCol, row = k % perCol;
        const float cx = x0 + col * (cw + gutter);
        const float y = top + lh * (row + 1);
        const Color dot = c.active ? SlotColor(i) : SlotColor(i).WithA(0.35f);
        const Color tc = c.active ? Pal::text : Pal::textFaint;

        // Slot number matches the badge drawn above the finger.
        r.Disc(cx + 4 * s, y + fh * 0.5f, 4 * s, dot);
        r.Textf(F_TINY, cx + 11 * s, y, c.active ? dot : Pal::textFaint, "%d", i + 1);

        const double hz = c.dt.n && c.dt.mean > 0 ? 1000.0 / c.dt.mean : 0.0;
        for (const Field& fd : fields)
        {
            const float fx = cx + cw * fd.f;
            switch (fd.kind)
            {
            case F_ID: r.Textf(F_TINY, fx, y, tc, "%u", c.pointerId); break;
            case F_X:  r.Textf(F_TINY, fx, y, tc, "%.0f", c.x); break;
            case F_Y:  r.Textf(F_TINY, fx, y, tc, "%.0f", c.y); break;
            case F_HZ: r.Textf(F_TINY, fx, y, hz > 0 ? RateColor(hz) : Pal::textFaint,
                               hz > 0 ? "%.0f" : "--", hz); break;
            case F_SD: r.Textf(F_TINY, fx, y, tc, c.dt.n ? "%.2f" : "--", c.dt.Sd()); break;
            case F_N:  r.Textf(F_TINY, fx, y, tc, "%llu", (unsigned long long)c.samples); break;
            }
        }
    }

    if (!total)
        r.Text(F_TINY, x0, top + lh, Pal::textFaint, "no contacts recorded yet");
    else if (shown < total)
    {
        // Live contacts come first, so normally only finished ones are cut.
        const int liveCut = std::max(0, live - shown);
        if (liveCut)
            r.TextRight(F_TINY, box.r() - 10 * s, box.y + 6 * s, Pal::warn,
                        "+%d live not shown", liveCut);
        else
            r.TextRight(F_TINY, box.r() - 10 * s, box.y + 6 * s, Pal::textFaint,
                        "+%d earlier", total - shown);
    }

    // Tap metrics at the bottom, but never at the cost of a contact row.
    if (showFooter)
    {
        const float fy = box.b() - lhNormal * 2.4f;
        r.FillRect(Rect2{ x0, fy - 4 * s, w, 1 }, Pal::edge);
        const Stats& ti = t.TapIntervalMs();
        const Stats& dw = t.DwellMs();
        r.Textf(F_TINY, x0, fy, Pal::textDim, ti.n ? "tap gap  %.1f ms  sd %.1f" : "tap gap  --",
                ti.mean, ti.Sd());
        r.Textf(F_TINY, x0, fy + lhNormal, Pal::textDim, dw.n ? "dwell    %.1f ms  sd %.1f" : "dwell    --",
                dw.mean, dw.Sd());
    }
}

// ============================================== footer + help

static void DrawFooter(App& app, const Rect2& box)
{
    Renderer& r = app.rend;
    const float s = r.Scale();
    r.FillRect(box, Pal::panel2.WithA(kBarA));
    r.FillRect(Rect2{ box.x, box.y, box.w, 1 }, Pal::edge);

    const float ty = box.y + (box.h - r.FontHeight(F_TINY)) * 0.5f;
    const float margin = 14 * s, gap = 24 * s;

    // Right side first: session time, then recording state.
    char sess[96], rec[96];
    const Tracker& t = app.Shown();
    snprintf(sess, sizeof sess, "session %.0f s   touching %.0f s",
             QpcToSec(app.nowQpc - app.startQpc), t.TouchingSec(app.nowQpc));
    const bool logging = app.tracker.LiveLogging();
    if (logging)
        snprintf(rec, sizeof rec, "* LOGGING %llu rows",
                 (unsigned long long)(app.tracker.LiveLogRows() + app.pad.LiveLogRows()));
    else
        snprintf(rec, sizeof rec, "%zu rows buffered", t.RecordCount());
    const float sessW = r.TextW(F_TINY, sess), recW = r.TextW(F_TINY, rec);

    // Key hints shorten to whatever fits beside the right-hand block.
    static const char* kHints[] = {
        "[S] export   [L] live log   [O] folder   [R] reset   [C] clear   [V] vsync   "
        "[H] history   [T] trails   [G] grid lines   [Tab] grid scan   [F] full screen   "
        "[F1] help   [Esc] quit",
        "[S] export   [L] live log   [R] reset   [C] clear   [Tab] grid scan   "
        "[F] full screen   [F1] help   [Esc] quit",
        "[S] export   [Tab] grid scan   [F] full screen   [F1] help   [Esc] quit",
        "[F1] help",
    };
    const int kNumHints = (int)(sizeof kHints / sizeof kHints[0]);

    // Recording state stays visible unless the window is too narrow for even
    // the shortest hint beside it; it is never dropped while logging.
    bool showRec = true;
    float avail = box.w - 2 * margin - sessW - gap - recW - gap;
    if (!logging && avail < r.TextW(F_TINY, kHints[kNumHints - 1]))
    {
        showRec = false;
        avail = box.w - 2 * margin - sessW - gap;
    }

    if (app.toast.until > app.nowQpc && !app.toast.text.empty())
    {
        r.Text(F_TINY, margin, ty, app.toast.color,
               FitText(r, F_TINY, app.toast.text, avail).c_str());
    }
    else
    {
        for (int i = 0; i < kNumHints; ++i)
            if (i == kNumHints - 1 || r.TextW(F_TINY, kHints[i]) <= avail)
            {
                if (r.TextW(F_TINY, kHints[i]) <= avail)
                    r.Text(F_TINY, margin, ty, Pal::textFaint, kHints[i]);
                break;
            }
    }

    float rx = box.r() - margin;
    r.Text(F_TINY, rx - sessW, ty, Pal::textFaint, sess);
    rx -= sessW + gap;
    if (showRec)
        r.Text(F_TINY, rx - recW, ty, logging ? Pal::good : Pal::textFaint, rec);
}

struct HelpKey { const char* k; const char* v; };

static const HelpKey kHelpKeys[] = {
    { "S",     "Export everything measured so far (CSV + report + JSON)" },
    { "L",     "Start / stop streaming every sample to a CSV file" },
    { "O",     "Open the export folder in Explorer" },
    { "R",     "Reset all measurements, keep the current contacts" },
    { "C",     "Clear the drawn ink and trails" },
    { "V",     "Toggle vsync (off = immediate present, lowest latency)" },
    { "H",     "Toggle coalesced-frame recovery" },
    { "T / G", "Toggle trails / background grid" },
    { "I / P", "Toggle accumulated ink / sample dots" },
    { "D",     "Re-enumerate touch devices" },
    { "F",     "Borderless full screen - also stops Windows taking edge touches" },
    { "Tab",   "Grid scan: paint the whole screen to find dead zones" },
    { "+ / -", "Grid scan: finer / coarser cells  (C clears the grid)" },
    { "F1",    "This help" },
    { "Esc",   "Quit - or leave the grid scan" },
};

static const char* kHelpNotes[] = {
    "Report rate counts distinct input frames from the",
    "digitizer, using the hardware timestamp carried on",
    "each pointer report. Windows coalesces reports when",
    "an app cannot keep up, so TouchRate replays the",
    "per-frame history to recover every one. Turn that",
    "off with [H] to see the delivery rate an app would",
    "observe reading only the newest sample per message.",
    "",
    "Delivery latency is the gap between that hardware",
    "timestamp and the moment this process dequeued the",
    "message. It covers driver and OS input handling",
    "only - never panel scan-out - so it is a floor for",
    "end-to-end latency, not a measurement of it. Photon",
    "latency needs external instrumentation.",
    "",
    "Raw HID reports come straight from the digitizer",
    "through Raw Input, bypassing the pointer stack.",
    "Matching counts mean nothing is being lost.",
    "",
    "A red cross marks a touch the panel reported that",
    "Windows never delivered. Outside full screen, the",
    "shell takes touches at the screen edges for swipes.",
    "",
    "A touch pad is measured on its own, from its HID",
    "reports, timed by the pad's own scan-time clock.",
    "The view follows whichever input was touched last.",
    "",
    "A pen is measured on its own too, while its tip is",
    "down. The ring at the tip and the width of its ink",
    "follow its pressure, as Windows passes it on: 0 to",
    "1024, whatever the pen resolves itself.",
    "",
    "Measured refresh times real vblanks on a dedicated",
    "thread, so it shows the panel's true rate rather",
    "than the rounded value in the mode table.",
};

static void DrawHelp(App& app)
{
    Renderer& r = app.rend;
    const float s = r.Scale();
    Rect2 full{ 0, 0, (float)r.Width(), (float)r.Height() };
    r.FillRect(full, Color(0, 0, 0, 0.80f));

    const int   nKeys = (int)_countof(kHelpKeys);
    const int   nNotes = (int)_countof(kHelpNotes);
    const float lhKey = std::round(r.FontHeight(F_BODY) * 1.25f);
    const float lhNote = std::round(r.FontHeight(F_TINY) * 1.25f);
    const float secH = std::round(r.FontHeight(F_TINY) * 1.9f);
    const float titleH = std::round(r.FontHeight(F_HEAD) * 1.75f);
    const float closeH = std::round(r.FontHeight(F_TINY) * 2.4f);
    const float pad = 26 * s;
    const float colGap = 34 * s;

    // Measure the real text so the panel is never smaller than its contents.
    float keyIndent = 0, keyTextW = 0, noteW = 0;
    for (const HelpKey& kv : kHelpKeys)
    {
        keyIndent = std::max(keyIndent, r.TextW(F_BODY, kv.k));
        keyTextW = std::max(keyTextW, r.TextW(F_BODY, kv.v));
    }
    keyIndent += 18 * s;
    for (const char* n : kHelpNotes) noteW = std::max(noteW, r.TextW(F_TINY, n));

    const float keysColW = keyIndent + keyTextW;
    const float keysH = secH + nKeys * lhKey;
    const float maxW = full.w - 40 * s;
    const float maxH = full.h - 40 * s;

    // Side-by-side when it fits, stacked otherwise.
    const bool twoCol = (pad * 2 + keysColW + colGap + noteW) <= maxW;

    auto heightFor = [&](int shownNotes) {
        const float nh = secH + shownNotes * lhNote;
        return pad + titleH + (twoCol ? std::max(keysH, nh) : keysH + 16 * s + nh)
             + closeH + pad;
    };

    // Drop trailing notes rather than let anything spill past the panel.
    int shownNotes = nNotes;
    float boxH = heightFor(shownNotes);
    while (boxH > maxH && shownNotes > 0) boxH = heightFor(--shownNotes);

    float boxW = twoCol ? (pad * 2 + keysColW + colGap + noteW)
                        : (pad * 2 + std::max(keysColW, noteW));
    boxW = std::min(boxW, maxW);

    Rect2 b{ std::round((full.w - boxW) * 0.5f), std::round((full.h - boxH) * 0.5f), boxW, boxH };
    r.FillRect(b, Pal::panel);
    r.FrameRect(b, 1.f, Pal::accent);

    const float x = b.x + pad;
    float y = b.y + pad;
    r.Text(F_HEAD, x, y, Pal::hero, "TouchRate - keys and what is measured");
    y += titleH;

    const float colY = y;
    r.Text(F_TINY, x, y, Pal::textFaint, "KEYS");
    y += secH;
    for (const HelpKey& kv : kHelpKeys)
    {
        r.Text(F_BODY, x, y, Pal::accent, kv.k);
        r.Text(F_BODY, x + keyIndent, y, Pal::text, kv.v);
        y += lhKey;
    }

    float nx = twoCol ? x + keysColW + colGap : x;
    float ny = twoCol ? colY : y + 16 * s;
    r.Text(F_TINY, nx, ny, Pal::textFaint, "WHAT THE NUMBERS MEAN");
    ny += secH;
    for (int i = 0; i < shownNotes; ++i)
    {
        r.Text(F_TINY, nx, ny, Pal::textDim, kHelpNotes[i]);
        ny += lhNote;
    }

    r.TextCenter(F_TINY, b.x + b.w * 0.5f, b.b() - closeH * 0.7f, Pal::textFaint,
                 "press F1 or Esc to close");
}

// ============================================== entry

// ============================================== grid scan screen

static void DrawGridScreen(App& app)
{
    Renderer& r = app.rend;
    const GridScan& g = app.grid;
    const Tracker& t = app.tracker;
    const float s = r.Scale();
    const POINT o = t.ClientOrigin();
    const Rect2 full{ 0, 0, (float)r.Width(), (float)r.Height() };

    // Contacts keep their markers here but lose the text readouts.
    app.freeArea = Rect2{};

    r.FillRect(full, Pal::bg);

    const int cols = g.Cols(), rows = g.Rows();
    auto cellRect = [&](int c, int rr) {
        const RECT rc = g.CellRect(c, rr);
        return Rect2{ (float)(rc.left - o.x), (float)(rc.top - o.y),
                      (float)(rc.right - rc.left), (float)(rc.bottom - rc.top) };
    };

    if (cols && rows)
    {
        // One touch proves a cell works, so touched cells start clearly lit and
        // only brighten a little further with use.
        const double top = std::log1p((double)std::max<uint32_t>(g.MaxHits(), 1));
        for (int rr = 0; rr < rows; ++rr)
            for (int c = 0; c < cols; ++c)
            {
                const uint32_t n = g.Hits(c, rr);
                if (n)
                {
                    const float k = top > 0 ? (float)(std::log1p((double)n) / top) : 1.f;
                    r.FillRect(cellRect(c, rr), Pal::good.WithA(0.32f + 0.38f * k));
                }
                else if (g.Frontier(c, rr))
                {
                    r.FillRect(cellRect(c, rr), Pal::bad.WithA(0.34f));
                }
            }

        const Color line = Pal::edge.WithA(0.9f);
        for (int c = 1; c < cols; ++c)
            r.FillRect(Rect2{ cellRect(c, 0).x, 0, 1, full.h }, line);
        for (int rr = 1; rr < rows; ++rr)
            r.FillRect(Rect2{ 0, cellRect(0, rr).y, full.w, 1 }, line);
    }

    // Instructions until the first touch lands.
    if (g.Samples() == 0)
    {
        const char* l1 = "Drag slowly over the whole screen, edges and corners included";
        const char* l2 = "Every cell a touch lands in turns green. Cells that stay dark or red never "
                         "registered a touch: those are dead zones.";
        const float w = std::max(r.TextW(F_HEAD, l1), r.TextW(F_TINY, l2)) + 48 * s;
        const float h = r.FontHeight(F_HEAD) + r.FontHeight(F_TINY) * 2.2f + 30 * s;
        const Rect2 b{ (full.w - w) * 0.5f, (full.h - h) * 0.5f, w, h };
        r.FillRect(b, Pal::panel.WithA(0.92f));
        r.FrameRect(b, 1.f, Pal::accent.WithA(0.7f));
        r.TextCenter(F_HEAD, full.w * 0.5f, b.y + 14 * s, Pal::text, "%s", l1);
        r.TextCenter(F_TINY, full.w * 0.5f, b.y + 22 * s + r.FontHeight(F_HEAD), Pal::textDim, "%s", l2);
    }

    // Lost touches matter most here: a cell can look dead because the panel
    // never sensed the finger, or because Windows took the touch.
    DrawUndelivered(app, full);
    DrawContacts(app, full);

    // ---- status bar: the only chrome on this screen, translucent over the grid
    const float barH = std::round(30 * s);
    const Rect2 bar{ 0, full.h - barH, full.w, barH };
    r.FillRect(bar, Pal::panel2.WithA(0.62f));
    r.FillRect(Rect2{ bar.x, bar.y, bar.w, 1 }, Pal::edge.WithA(0.8f));
    const float ty = bar.y + (bar.h - r.FontHeight(F_TINY)) * 0.5f;
    const float gap = 22 * s;
    const float margin = 14 * s;

    struct Seg { std::string text; Color color; bool optional; };
    std::vector<Seg> segs;
    char b[160];

    segs.push_back({ "GRID SCAN", Pal::accent, false });
    const int untouched = g.Cells() - g.Touched();
    snprintf(b, sizeof b, "%d / %d cells touched  %.1f%%", g.Touched(), g.Cells(), g.Coverage() * 100.0);
    segs.push_back({ b, untouched == 0 && g.Cells() ? Pal::good : Pal::text, false });
    if (g.Samples())
    {
        snprintf(b, sizeof b, "%d untouched", untouched);
        segs.push_back({ b, untouched ? Pal::warn : Pal::good, false });
    }
    if (t.HidUndelivered())
    {
        snprintf(b, sizeof b, "%llu touch%s not delivered by Windows",
                 (unsigned long long)t.HidUndelivered(), t.HidUndelivered() == 1 ? "" : "es");
        segs.push_back({ b, Pal::bad, false });
    }
    if (cols && rows)
    {
        snprintf(b, sizeof b, "%d x %d cells of ~%d x %d px", cols, rows,
                 (int)std::lround((double)(g.Area().right - g.Area().left) / cols),
                 (int)std::lround((double)(g.Area().bottom - g.Area().top) / rows));
        segs.push_back({ b, Pal::textFaint, true });
    }

    const bool toast = app.toast.until > app.nowQpc && !app.toast.text.empty();
    const char* right = toast ? app.toast.text.c_str()
        : "[+/-] cell size   [C] clear   [S] export   [Tab] analyzer   [F1] help   [Esc] back";
    const Color rightColor = toast ? app.toast.color : Pal::textFaint;
    const float rightW = r.TextW(F_TINY, right);

    // Drop the optional geometry note before anything collides.
    auto leftWidth = [&](bool withOptional) {
        float w = 0;
        for (const Seg& sg : segs)
            if (withOptional || !sg.optional) w += r.TextW(F_TINY, sg.text.c_str()) + gap;
        return w;
    };
    const bool keepOptional = margin + leftWidth(true) + rightW + margin <= bar.w;

    float x = margin;
    for (const Seg& sg : segs)
    {
        if (sg.optional && !keepOptional) continue;
        r.Text(F_TINY, x, ty, sg.color, sg.text.c_str());
        x += r.TextW(F_TINY, sg.text.c_str()) + gap;
    }
    // A toast is shortened to the space left; key hints give way entirely.
    const float room = bar.r() - margin - x;
    if (toast)
        r.TextRight(F_TINY, bar.r() - margin, ty, rightColor, "%s",
                    FitText(r, F_TINY, right, room).c_str());
    else if (rightW <= room)
        r.TextRight(F_TINY, bar.r() - margin, ty, rightColor, "%s", right);
}

// Smallest client area at which every panel fits: header, footer, the graph
// strip tall enough for ten contact rows, the stats column in its compact
// spacing above it, and wide enough for the stats values.
void MinClientSize(const Renderer& r, int& w, int& h)
{
    const float s = r.Scale();
    w = (int)std::ceil(kMinClientW * s);
    h = (int)std::ceil(std::round(kHeaderH * s) + std::round(kFooterH * s) +
                       std::max(std::round(kStripMin * s), TableHeight(r, kTableRows)) +
                       MeasureStats(r, true).Need());
}

void DrawUi(App& app)
{
    if (app.gridMode)
    {
        DrawGridScreen(app);
        if (app.view.showHelp) DrawHelp(app);
        return;
    }

    Layout L = ComputeLayout(app.rend);
    app.freeArea = L.canvasFree;

    // Canvas underneath, panels over it, then contacts over everything.
    DrawCanvas(app, L.canvas);
    if (app.ShowingPad()) DrawPad(app, L.canvasFree);
    DrawHeader(app, L.header);
    DrawStats(app, L.stats);
    DrawHistogram(app, L.histo);
    DrawRateGraph(app, L.graph);
    if (app.ShowingPen()) DrawPressure(app, L.table);
    else DrawTable(app, L.table);
    DrawFooter(app, L.footer);
    DrawContacts(app, L.canvas);
    DrawPen(app, L.canvas);
    DrawUndelivered(app, L.canvas);
    if (app.view.showHelp) DrawHelp(app);
}
