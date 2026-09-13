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
    m.heroMin  = r.FontHeight(F_HEAD) + m.chrome;
    m.heroMax  = r.FontHeight(F_HERO) + m.chrome;
    m.fixedH   = m.hTiming + m.hDeliver + m.hDisplay + m.hMulti + m.gap * 4 + m.pad * 2;
    return m;
}

Layout ComputeLayout(const Renderer& r)
{
    const float s = r.Scale();
    const float W = (float)r.Width(), H = (float)r.Height();

    Layout L;
    const float headerH = std::round(86 * s);
    const float footerH = std::round(26 * s);
    float stripH = std::round(std::min(200.f * s, std::max(140.f * s, H * 0.22f)));
    const float statsW  = std::round(std::min(560.f * s, std::max(320.f * s, W * 0.36f)));

    // If even compact spacing cannot fit the stats column, take the difference
    // from the graph strip, which degrades far more gracefully.
    const float compactNeed = MeasureStats(r, true).Need();
    if (H - headerH - footerH - stripH < compactNeed)
        stripH = std::round(std::max(110.f * s, H - headerH - footerH - compactNeed));

    L.header = { 0, 0, W, headerH };
    L.footer = { 0, H - footerH, W, footerH };

    const float midY = headerH;
    const float midH = std::max(40.f, H - headerH - footerH - stripH);

    L.canvas     = { 0, 0, W, H };
    L.canvasFree = { 0, midY, W - statsW, midH };
    L.stats      = { W - statsW, midY, statsW, midH };

    const float stripY = midY + midH;
    const float tableW = std::round(std::min(470.f * s, W * 0.34f));
    const float restW  = W - tableW;
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
        r.Text(F_BODY, x, y, Pal::textDim, label);
        r.Text(F_BODY, x + labelW, y, vc, buf);
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
        r.Text(F_TINY, box.x + 10 * s, box.y + 6 * s, Pal::accent, title);
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

    float x = 14 * s, y = 8 * s;
    r.Text(F_HEAD, x, y, Pal::hero, "TouchRate");
    float tw = r.TextW(F_HEAD, "TouchRate");
    r.Text(F_TINY, x + tw + 10 * s, y + r.FontHeight(F_HEAD) - r.FontHeight(F_TINY) - 2 * s,
           Pal::textFaint, "touch polling rate / latency / 10-finger analyzer");

    y += std::round(r.FontHeight(F_HEAD) * 1.05f);
    const float lh = std::round(r.FontHeight(F_BODY) * 1.12f);

    if (app.devices.empty())
    {
        r.Text(F_BODY, x, y, Pal::bad, "No touch digitizer found.");
        r.Text(F_BODY, x, y + lh, Pal::textDim,
               "Connect a touch screen, then press [D] to re-enumerate.");
        return;
    }

    const int di = app.activeDevice >= 0 ? app.activeDevice : 0;
    const TouchDevice& d = app.devices[(size_t)di];

    // Line 1: identity
    std::string name = d.Label();
    r.Text(F_BODY, x, y, Pal::text, name.c_str());
    float cx = x + r.TextW(F_BODY, name.c_str()) + 16 * s;

    r.Text(F_BODY, cx, y, Pal::accent, d.VidPidString().c_str());
    cx += r.TextW(F_BODY, d.VidPidString().c_str()) + 16 * s;

    if (d.version)
    {
        char vb[32]; snprintf(vb, sizeof vb, "rev 0x%04X", d.version);
        r.Text(F_BODY, cx, y, Pal::textDim, vb);
        cx += r.TextW(F_BODY, vb) + 16 * s;
    }
    {
        char tb[96];
        snprintf(tb, sizeof tb, "%s", WideToUtf8(d.typeName).c_str());
        r.Text(F_BODY, cx, y, Pal::textDim, tb);
        cx += r.TextW(F_BODY, tb) + 16 * s;
    }
    if (app.activeDevice < 0 && app.devices.size() > 1)
        r.Text(F_TINY, cx, y + 2 * s, Pal::textFaint, "(no reports yet - touch the screen)");
    else if (app.devices.size() > 1)
    {
        char mb[64];
        snprintf(mb, sizeof mb, "+%zu more digitizer%s", app.devices.size() - 1,
                 app.devices.size() == 2 ? "" : "s");
        r.Text(F_TINY, cx, y + 2 * s, Pal::textFaint, mb);
    }

    // Line 2: capability and geometry
    y += lh;
    char buf[512];
    int n = 0;
    n += snprintf(buf + n, sizeof buf - n, "contacts %u",
                  d.maxContacts ? d.maxContacts : d.hidMaxContacts);
    if (d.hidMaxContacts && d.maxContacts && d.hidMaxContacts != d.maxContacts)
        n += snprintf(buf + n, sizeof buf - n, " (HID %u)", d.hidMaxContacts);
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
    if (d.inputReportBytes)
        n += snprintf(buf + n, sizeof buf - n, "   report %u B", d.inputReportBytes);
    r.Text(F_TINY, x, y + 2 * s, Pal::textDim, buf);

    // Right side: display identity
    const MonitorInfo& m = app.monitor;
    std::string mon = m.friendlyName.empty() ? WideToUtf8(m.gdiName) : WideToUtf8(m.friendlyName);
    char rb[256];
    snprintf(rb, sizeof rb, "%s   %dx%d   %.3f Hz nominal   DPI %u",
             mon.c_str(), m.width, m.height, m.nominalHz, m.dpi);
    r.TextRight(F_TINY, box.r() - 14 * s, 10 * s, Pal::textDim, "%s", rb);
    r.TextRight(F_TINY, box.r() - 14 * s, 10 * s + r.FontHeight(F_TINY) * 1.4f,
                Pal::textFaint, "%s", r.AdapterName());
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
        const std::vector<InkPt>& ink = t.Ink();
        const uint64_t total = t.InkTotal();
        const size_t cap = ink.size();
        uint64_t fresh = total - app.inkDrawn;
        if (fresh > (uint64_t)t.InkCount()) fresh = (uint64_t)t.InkCount();

        if (fresh && r.HasInkLayer())
        {
            const size_t start = (t.InkHead() + cap - (size_t)fresh) % cap;
            const float dot = std::max(1.f, std::round(1.f * s));
            r.BeginInk();
            r.SetClip(box);
            for (size_t i = 0; i < (size_t)fresh; ++i)
            {
                const InkPt& p = ink[(start + i) % cap];
                if (!box.Contains(p.x, p.y)) continue;
                r.FillRect(Rect2{ p.x, p.y, dot, dot },
                           SlotColor(p.slot).WithA(p.fromHistory ? 0.35f : 0.60f));
            }
            r.EndInk();
        }
        app.inkDrawn = total;

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
    }

    if (t.ContactsNow() == 0)
    {
        const Rect2& f = app.freeArea;
        const char* msg = app.devices.empty()
            ? "No touch device detected"
            : "Touch anywhere to begin measuring";
        r.TextCenter(F_HEAD, f.x + f.w * 0.5f,
                     f.y + f.h * 0.5f - r.FontHeight(F_HEAD),
                     Pal::textFaint, "%s", msg);
        if (!app.devices.empty())
            r.TextCenter(F_TINY, f.x + f.w * 0.5f, f.y + f.h * 0.5f + 4 * s,
                         Pal::textFaint,
                         "Drag slowly to see sample spacing. Press all ten fingers for the contact test.");
    }

    r.ClearClip();
}

// Contacts are drawn after every panel so a finger resting on the stats column
// is still visible; the translucent panels alone would dim it too far.
static void DrawContacts(App& app, const Rect2& box)
{
    Renderer& r = app.rend;
    Tracker& t = app.tracker;
    const float s = r.Scale();

    for (int i = 0; i < kMaxSlots; ++i)
    {
        const Contact& c = t.Slot(i);
        if (!c.active) continue;
        const Color col = SlotColor(i);

        r.FillRect(Rect2{ box.x, std::round(c.y), box.w, 1 }, col.WithA(0.22f));
        r.FillRect(Rect2{ std::round(c.x), box.y, 1, box.h }, col.WithA(0.22f));

        float rad = 26 * s;
        if (c.cw > 0) rad = Clampf(std::max(c.cw, c.ch) * 0.5f, 12 * s, 90 * s);
        r.Glow(c.x, c.y, rad * 1.9f, col.WithA(0.20f));
        r.Disc(c.x, c.y, rad * 0.30f, col.WithA(0.98f));
        r.RingShape(c.x, c.y, rad, col.WithA(0.92f));
        if (c.pressure >= 0)
            r.RingShape(c.x, c.y, rad * (0.4f + 0.6f * Clampf(c.pressure, 0.f, 1.f)),
                        col.WithA(0.35f));

        // Slot badge stays wherever the finger is; it is one glyph and is what
        // identifies the contact.
        char id[8]; snprintf(id, sizeof id, "%d", i + 1);
        r.TextCenter(F_HEAD, c.x, c.y - rad - r.FontHeight(F_HEAD) - 4 * s, col, "%s", id);

        // The multi-line readout would sit on top of panel figures and make
        // both unreadable, so it is shown only over open canvas.
        if (!app.freeArea.Contains(c.x, c.y)) continue;

        float lx = c.x + rad + 8 * s, ly = c.y - r.FontHeight(F_TINY);
        if (lx + 190 * s > app.freeArea.r()) lx = c.x - rad - 8 * s - 190 * s;
        r.Textf(F_TINY, lx, ly, Pal::text, "%.0f, %.0f px", c.x, c.y);
        if (c.instHz > 0)
            r.Textf(F_TINY, lx, ly + r.FontHeight(F_TINY) * 1.2f, RateColor(c.instHz),
                    "%.0f Hz  id %u", c.instHz, c.pointerId);
        else
            r.Textf(F_TINY, lx, ly + r.FontHeight(F_TINY) * 1.2f, Pal::textFaint,
                    "id %u", c.pointerId);
        if (c.pressure >= 0)
            r.Textf(F_TINY, lx, ly + r.FontHeight(F_TINY) * 2.4f, Pal::textDim,
                    "p %.2f", c.pressure);
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
    // gap between a raw report and its pointer message has clearly passed.
    const float pulse = 0.55f + 0.45f * (float)std::sin(QpcToSec(now) * 12.0);
    for (const HidTrack& h : t.HidTracks())
    {
        if (h.delivered || h.ended || !h.mapped) continue;
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

// ================================================================ stats panel

static void DrawStats(App& app, const Rect2& box)
{
    Renderer& r = app.rend;
    Tracker& t = app.tracker;
    const float s = r.Scale();
    const int64_t now = app.nowQpc;

    r.FillRect(box, Pal::panel2.WithA(kColumnA));
    r.FillRect(Rect2{ box.x, box.y, 1, box.h }, Pal::edge);

    StatsMetrics m = MeasureStats(r, false);
    if (box.h < m.Need()) m = MeasureStats(r, true);
    const float pad = m.pad, rowH = m.rowH, titleH = m.titleH, gap = m.gap, pipR = m.pipR;
    const float hTiming = m.hTiming, hDeliver = m.hDeliver, hDisplay = m.hDisplay, hMulti = m.hMulti;
    const float chrome = m.chrome;
    const float bw = box.w - pad * 2;
    const float bx = box.x + pad;

    const double liveHz = t.FrameHz(now);
    const double modeHz = t.ModeHz();
    const double meanHz = t.AvgHz();
    const Stats& iv = t.IntervalMs();
    const double periodMs = modeHz > 0 ? 1000.0 / modeHz : 0.0;

    // Give the hero block whatever is left after the fixed-size blocks, so a
    // short window shrinks the big number instead of clipping the last panel.
    float heroH = box.h - m.fixedH - m.hCounters;
    bool roomForCounters = heroH >= m.heroMin;
    if (!roomForCounters) heroH = box.h - m.fixedH;        // drop the counters first
    heroH = Clampf(heroH, m.heroMin, m.heroMax);
    (void)chrome;

    float y = box.y + pad;

    // ---- hero: report rate
    {
        Rect2 hb{ bx, y, bw, heroH };
        Block(r, hb, "TOUCH REPORT RATE  (input frames per second)", kBlockA);

        // Use the big face only when the block is tall enough to hold it.
        const int face = (heroH >= r.FontHeight(F_HERO) + chrome) ? F_HERO : F_HEAD;
        Color c = RateColor(liveHz > 0 ? liveHz : modeHz);
        char num[32];
        if (liveHz > 0)       snprintf(num, sizeof num, "%.0f", liveHz);
        else if (modeHz > 0)  snprintf(num, sizeof num, "%.0f", modeHz);
        else                  snprintf(num, sizeof num, "--");

        const float ny = hb.y + r.FontHeight(F_TINY) * 1.6f;
        r.Text(face, hb.x + 10 * s, ny, c, num);
        const float nw = r.TextW(face, num);
        r.Text(F_BODY, hb.x + 10 * s + nw + 8 * s,
               ny + r.FontHeight(face) - r.FontHeight(F_BODY) - 3 * s, Pal::textDim, "Hz");

        // modal / mean stacked at the right, centred against the big number
        const float rx = hb.r() - 12 * s;
        const float pairH = r.FontHeight(F_BODY) * 1.25f;
        float ry = ny + (r.FontHeight(face) - pairH * 2) * 0.5f;
        r.TextRight(F_BODY, rx, ry, modeHz > 0 ? Pal::text : Pal::textFaint,
                    modeHz > 0 ? "modal  %.1f Hz" : "modal  -- Hz", modeHz);
        r.TextRight(F_BODY, rx, ry + pairH, meanHz > 0 ? Pal::textDim : Pal::textFaint,
                    meanHz > 0 ? "mean   %.1f Hz" : "mean   -- Hz", meanHz);
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
        // Raw HID and delivery share a row: a lost touch is the thing to see.
        if (!t.HidSeen())
            c.Row("raw HID reports", Pal::textFaint, "not observed");
        else if (t.HidUndelivered())
            c.Row("raw HID reports", Pal::bad, "%.0f /s   %llu touch%s lost", hidHz,
                  (unsigned long long)t.HidUndelivered(), t.HidUndelivered() == 1 ? "" : "es");
        else if (t.HidContacts())
            c.Row("raw HID reports", Pal::accent, "%.0f /s   none lost", hidHz);
        else
            c.Row("raw HID reports", Pal::accent, "%.0f /s", hidHz);
        y = b.b() + gap;
    }

    // ---- delivery
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

    // ---- contacts / ten finger test
    {
        Rect2 b{ bx, y, bw, hMulti };
        Block(r, b, "MULTI-TOUCH  (10 finger test, modal Hz per count)", kBlockA);
        Col c(r, Rect2{ b.x + 10 * s, b.y + titleH, bw - 20 * s, 0 });
        c.lh = rowH;

        // The HID descriptor is the hardware truth; Windows often advertises a
        // larger figure than the panel can actually report at once.
        int devMax = 0;
        const TouchDevice* dev = nullptr;
        if (app.activeDevice >= 0) dev = &app.devices[(size_t)app.activeDevice];
        else if (!app.devices.empty()) dev = &app.devices[0];
        if (dev) devMax = (int)(dev->hidMaxContacts ? dev->hidMaxContacts : dev->maxContacts);

        c.Row("contacts now", t.ContactsNow() ? Pal::good : Pal::textFaint,
              "%d", t.ContactsNow());
        Color mc = (devMax && t.MaxSimultaneous() >= devMax) ? Pal::good
                 : (t.MaxSimultaneous() >= 10 ? Pal::good : Pal::warn);
        if (devMax) c.Row("max reached / device", mc, "%d  /  %d", t.MaxSimultaneous(), devMax);
        else        c.Row("max reached", mc, "%d", t.MaxSimultaneous());

        float px = b.x + 12 * s + pipR;
        float py = c.y + pipR + 4 * s;
        const float pipStep = (bw - 24 * s - pipR * 2) / 9.f;
        // Rate measured at each contact count, so a panel that slows down as
        // fingers are added shows it here rather than only in the report.
        const float hzY = py + pipR + 4 * s;
        double bestHz = 0;
        for (int i = 1; i <= 10; ++i)
            if (t.HasDataAt(i)) bestHz = std::max(bestHz, t.ModeHzAt(i));

        for (int i = 1; i <= 10; ++i)
        {
            bool hit = t.SawSimultaneous(i);
            float cxp = px + pipStep * (i - 1);

            if (t.HasDataAt(i))
            {
                double hz = t.ModeHzAt(i);
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

    if (!t.UseHistory())
        r.TextRight(F_TINY, box.r() - pad, box.b() - r.FontHeight(F_TINY) - 2 * s, Pal::warn,
                    "history recovery OFF - showing delivered rate");
    else if (t.LostContacts())
        r.TextRight(F_TINY, box.r() - pad, box.b() - r.FontHeight(F_TINY) - 2 * s, Pal::bad,
                    "%llu contact%s lost its up message",
                    (unsigned long long)t.LostContacts(), t.LostContacts() == 1 ? "" : "s");
}

// ============================================== interval histogram

static void DrawHistogram(App& app, const Rect2& box)
{
    Renderer& r = app.rend;
    const Histogram& h = app.tracker.IntervalHist();
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
        r.TextCenter(F_TINY, x, plot.b() + 6 * s, Pal::textFaint, "%.2f", ms);
    }
    r.Text(F_TINY, plot.x, plot.b() + 6 * s + r.FontHeight(F_TINY) * 1.15f, Pal::textFaint, "ms");

    // annotate the mode
    if (modeBin >= lo && modeBin <= hi)
    {
        double ms = h.BinCenter(modeBin);
        float x = xOf(ms);
        r.FillRect(Rect2{ std::round(x), plot.y, 1, plot.h }, Pal::accent.WithA(0.5f));
        char lab[64];
        snprintf(lab, sizeof lab, "%.2f ms = %.0f Hz", ms, ms > 0 ? 1000.0 / ms : 0.0);
        float lw = r.TextW(F_TINY, lab);
        float lx = Clampf(x - lw * 0.5f, plot.x, plot.r() - lw);
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
    const Ring& ring = app.tracker.HzRing();
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

    // gridlines
    const double modeHz = app.tracker.ModeHz();
    for (int k = 1; k <= 4; ++k)
    {
        float v = top * k / 4.f;
        float gy = plot.b() - (v / top) * plot.h;
        r.FillRect(Rect2{ plot.x, std::round(gy), plot.w, 1 }, Pal::grid);
        r.TextRight(F_TINY, plot.x - 6 * s, gy - r.FontHeight(F_TINY) * 0.5f, Pal::textFaint,
                    "%.0f", v);
    }
    if (modeHz > 0 && modeHz < top)
    {
        float gy = plot.b() - (float)(modeHz / top) * plot.h;
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

// ============================================== per contact table

static void DrawTable(App& app, const Rect2& box)
{
    Renderer& r = app.rend;
    Tracker& t = app.tracker;
    const float s = r.Scale();

    Block(r, box, "PER-CONTACT DETAIL");

    const float lh = std::round(r.FontHeight(F_TINY) * 1.18f);
    float y = box.y + 26 * s;
    const float x0 = box.x + 10 * s;
    const float w = box.w - 20 * s;

    struct C { const char* h; float f; };
    const C cols[] = { {"#", 0.00f}, {"id", 0.10f}, {"x", 0.28f}, {"y", 0.43f},
                       {"Hz", 0.60f}, {"sd ms", 0.75f}, {"n", 0.90f} };
    for (const C& c : cols)
        r.Text(F_TINY, x0 + w * c.f, y, Pal::textFaint, c.h);
    y += lh;
    r.FillRect(Rect2{ x0, y - 2 * s, w, 1 }, Pal::edge);

    // Collect live contacts first: during a ten-finger test those are the rows
    // that matter, and the panel rarely has room for every slot ever used.
    int order[kMaxSlots];
    int total = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < kMaxSlots; ++i)
        {
            const Contact& c = t.Slot(i);
            if (!c.everUsed) continue;
            if ((pass == 0) != c.active) continue;
            order[total++] = i;
        }

    int rows = 0;
    const float footerH = r.FontHeight(F_TINY) * 2.6f;
    int maxRows = (int)((box.b() - y - 6 * s) / lh);
    // Give the tap/dwell footer its space only if every contact still fits.
    if (total > maxRows - 2) maxRows = (int)((box.b() - y - 6 * s) / lh);
    else maxRows = (int)((box.b() - y - footerH - 6 * s) / lh);
    const bool showFooter = total <= maxRows;

    for (int k = 0; k < total && rows < maxRows; ++k)
    {
        const int i = order[k];
        const Contact& c = t.Slot(i);
        Color col = c.active ? SlotColor(i) : SlotColor(i).WithA(0.35f);
        Color tc = c.active ? Pal::text : Pal::textFaint;

        r.Disc(x0 + 4 * s, y + r.FontHeight(F_TINY) * 0.5f, 4 * s, col);
        r.Textf(F_TINY, x0 + w * cols[1].f, y, tc, "%u", c.pointerId);
        r.Textf(F_TINY, x0 + w * cols[2].f, y, tc, "%.0f", c.x);
        r.Textf(F_TINY, x0 + w * cols[3].f, y, tc, "%.0f", c.y);
        double hz = c.dt.n && c.dt.mean > 0 ? 1000.0 / c.dt.mean : 0.0;
        r.Textf(F_TINY, x0 + w * cols[4].f, y, hz > 0 ? RateColor(hz) : Pal::textFaint,
                hz > 0 ? "%.0f" : "--", hz);
        r.Textf(F_TINY, x0 + w * cols[5].f, y, tc, c.dt.n ? "%.2f" : "--", c.dt.Sd());
        r.Textf(F_TINY, x0 + w * cols[6].f, y, tc, "%llu", (unsigned long long)c.samples);
        y += lh;
        ++rows;
    }
    if (!rows)
        r.Text(F_TINY, x0, y, Pal::textFaint, "no contacts recorded yet");
    else if (rows < total)
        r.Text(F_TINY, x0, y, Pal::textFaint, "...");

    // Tap metrics at the bottom, but never at the cost of a contact row.
    if (showFooter)
    {
        y = box.b() - lh * 2.4f;
        r.FillRect(Rect2{ x0, y - 4 * s, w, 1 }, Pal::edge);
        const Stats& ti = t.TapIntervalMs();
        const Stats& dw = t.DwellMs();
        r.Textf(F_TINY, x0, y, Pal::textDim, ti.n ? "tap gap  %.1f ms  sd %.1f" : "tap gap  --",
                ti.mean, ti.Sd());
        r.Textf(F_TINY, x0, y + lh, Pal::textDim, dw.n ? "dwell    %.1f ms  sd %.1f" : "dwell    --",
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

    float ty = box.y + (box.h - r.FontHeight(F_TINY)) * 0.5f;

    if (app.toast.until > app.nowQpc && !app.toast.text.empty())
    {
        r.Text(F_TINY, 14 * s, ty, app.toast.color, app.toast.text.c_str());
    }
    else
    {
        r.Text(F_TINY, 14 * s, ty, Pal::textFaint,
               "[S] export   [L] live log   [O] open folder   [R] reset   [C] clear   "
               "[V] vsync   [H] history   [T] trails   [G] grid   [F] full screen   "
               "[F1] help   [Esc] quit");
    }

    double sess = QpcToSec(app.nowQpc - app.startQpc);
    float rx = box.r() - 14 * s;
    r.TextRight(F_TINY, rx, ty, Pal::textFaint,
                "session %.0f s   touching %.0f s", sess, app.tracker.TouchingSec());
    rx -= r.TextW(F_TINY, "session 0000 s   touching 0000 s") + 18 * s;

    // Recording state has to stay visible whatever else the footer is saying.
    char rec[96];
    if (app.tracker.LiveLogging())
        snprintf(rec, sizeof rec, "* LOGGING %llu rows", (unsigned long long)app.tracker.LiveLogRows());
    else
        snprintf(rec, sizeof rec, "%zu rows buffered", app.tracker.RecordCount());
    r.TextRight(F_TINY, rx, ty, app.tracker.LiveLogging() ? Pal::good : Pal::textFaint, "%s", rec);
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
    { "F1",    "This help" },
    { "Esc",   "Quit" },
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

void DrawUi(App& app)
{
    Layout L = ComputeLayout(app.rend);
    app.freeArea = L.canvasFree;

    // Canvas underneath, panels over it, then contacts over everything.
    DrawCanvas(app, L.canvas);
    DrawHeader(app, L.header);
    DrawStats(app, L.stats);
    DrawHistogram(app, L.histo);
    DrawRateGraph(app, L.graph);
    DrawTable(app, L.table);
    DrawFooter(app, L.footer);
    DrawContacts(app, L.canvas);
    DrawUndelivered(app, L.canvas);
    if (app.view.showHelp) DrawHelp(app);
}
