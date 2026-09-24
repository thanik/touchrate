#include "tracker.h"

const char* SampleKindName(uint8_t k)
{
    switch (k) { case SK_DOWN: return "down"; case SK_UP: return "up"; default: return "move"; }
}

// ------------------------------------------------------------------ lifecycle

void Tracker::Init(size_t exportCapacity, Source source)
{
    m_source = source;
    if (exportCapacity < 1000) exportCapacity = 1000;
    m_recs.assign(exportCapacity, ExportRec{});
    m_recHead = m_recCount = 0;
    m_recDropped = 0;

    m_ink.assign(kInkCap, InkPt{});
    m_inkHead = m_inkCount = 0;

    m_frameRate.Init(); m_sampleRate.Init(); m_msgRate.Init(); m_hidRate.Init();
    for (Contact& c : m_slots) c.rate.Init();

    m_intervalHist.Init(0.0, 0.1, 300);   // 0 .. 30 ms at 0.1 ms
    m_latencyHist.Init(0.0, 0.2, 300);    // 0 .. 60 ms at 0.2 ms
    for (int i = 0; i <= kMaxSlots; ++i) m_intervalHistByCount[i].Init(0.0, 0.1, 300);
    m_hzRing.Init(2048);
    m_scratch.reserve(512 * 16);

    m_hoverRate.Init();
    m_hoverHist.Init(0.0, 0.1, 300);
    m_pressureRing.Init(2048);
    m_levelSeen.assign(1025, 0);
    m_penScratch.reserve(512);
}

void Tracker::ResetStats()
{
    m_intervalMs.Reset(); m_latencyMs.Reset(); m_predictPx.Reset();
    m_tapIntervalMs.Reset(); m_dwellMs.Reset();
    m_intervalHist.Reset(); m_latencyHist.Reset();
    for (int i = 0; i <= kMaxSlots; ++i)
    {
        m_intervalByCount[i].Reset();
        m_intervalHistByCount[i].Reset();
        m_maxGapByCount[i] = 0;
    }
    m_hzRing.Reset();
    m_frameRate.Reset(); m_sampleRate.Reset(); m_msgRate.Reset(); m_hidRate.Reset();
    m_maxGapMs = 0;
    m_touchingQpc = 0;
    m_touchingSince = m_contactsNow > 0 ? QpcNow() : 0;
    m_totalSamples = m_totalFrames = m_historySamples = 0;
    m_messages = m_hidReports = m_downs = m_ups = m_lostContacts = 0;
    m_lastFrameQpc = 0;
    m_hidTracks.clear();
    m_undelivered.clear();
    m_undeliveredMarkStart = 0;
    m_undeliveredDropped = 0;
    m_hidFrames = m_hidTouchReports = m_hidNoTipReports = m_hidEmptyReports = 0;
    m_hidContacts = m_hidDelivered = m_hidUndelivered = 0;
    m_hidSplitReports = 0;
    m_hidMaxContactCount = 0;
    m_hidOffWindow = m_hidOffWindowBlocked = 0;
    m_hidMatchOffset.Reset(); m_deliveredEdge.Reset(); m_undeliveredEdge.Reset();
    m_maxSimultaneous = m_contactsNow;
    for (int i = 0; i <= kMaxSlots; ++i) m_simSeen[i] = false;
    if (m_contactsNow >= 1 && m_contactsNow <= kMaxSlots) m_simSeen[m_contactsNow] = true;
    m_recHead = m_recCount = 0;
    m_recDropped = 0;
    m_hmSeen = false;
    for (Contact& c : m_slots) { c.dt.Reset(); c.samples = 0; c.rate.Reset(); c.pathLenPx = 0; }

    m_hidPenHeld = 0;
    m_pressure.Reset(); m_pressureAtDown.Reset(); m_tiltX.Reset(); m_tiltY.Reset();
    std::fill(m_levelSeen.begin(), m_levelSeen.end(), (uint8_t)0);
    m_pressureLevels = 0;
    m_pressureRing.Reset();
    m_rotationSeen = false;
    m_penFlagsSeen = 0;
    m_hoverIntervalMs.Reset(); m_hoverHist.Reset(); m_hoverRate.Reset();
    m_hoverReports = 0;
    m_lastHoverDevQpc = 0;
}

void Tracker::ClearInk()
{
    m_inkHead = m_inkCount = 0;
    for (Contact& c : m_slots) c.ClearTrail();
    m_undeliveredMarkStart = m_undelivered.size();   // markers only; counts stay
}

// ----------------------------------------------------------------- slot logic

int Tracker::FindSlot(uint32_t pointerId) const
{
    for (int i = 0; i < kMaxSlots; ++i)
        if (m_slots[i].active && m_slots[i].pointerId == pointerId) return i;
    return -1;
}

int Tracker::AssignSlot(uint32_t pointerId)
{
    int s = FindSlot(pointerId);
    if (s >= 0) return s;
    for (int i = 0; i < kMaxSlots; ++i)
    {
        if (!m_slots[i].active)
        {
            Contact& c = m_slots[i];
            bool reused = c.pointerId != pointerId;
            c = Contact{};
            c.rate.Init();
            c.active = true;
            c.everUsed = true;
            c.pointerId = pointerId;
            if (reused) c.ClearTrail();
            return i;
        }
    }
    return -1;
}

void Tracker::ReleaseSlot(int slot, int64_t qpc, bool genuine)
{
    if (slot < 0 || slot >= kMaxSlots) return;
    Contact& c = m_slots[slot];
    if (!c.active) return;
    c.active = false;
    c.upQpc = qpc;
    if (IsPen()) m_pressureRing.Push(-1.f);   // a break in the pressure trace
    if (genuine)
    {
        if (c.downQpc) m_dwellMs.Add(QpcToMs(qpc - c.downQpc));
        ++m_ups;
    }
    else
    {
        // Timed out rather than lifted; its dwell is an artefact of the
        // watchdog, so it must not enter the statistics.
        ++m_lostContacts;
    }
}

// -------------------------------------------------------------- message entry

int Tracker::HandlePointerMessage(UINT msg, WPARAM wParam, int64_t hostQpc)
{
    const uint32_t pid = GET_POINTERID_WPARAM(wParam);

    ++m_messages;
    m_msgRate.Tick(hostQpc);

    // The window procedure routes each pointer type to its own tracker.
    const bool isUp = (msg == WM_POINTERUP);
    int accepted = IsPen() ? IngestPenFrames(pid, hostQpc, isUp) : IngestFrames(pid, hostQpc, isUp);

    // Skipping the rest of an input frame is safe for moves, where the frame
    // history already carried every contact. It is not safe around a down or an
    // up: another contact's transition can share the frame and would be lost,
    // stranding that contact as permanently held.
    if (msg == WM_POINTERUPDATE) SkipPointerFrameMessages(pid);
    return accepted;
}

// ------------------------------------------------------------ HID delivery

namespace {

constexpr double kMatchRadiusPx = 96.0;   // a finger, with room for motion between reports
constexpr double kLiftTimeoutMs = 120.0;  // no touching report for this long means lifted
constexpr double kSettleMs      = 250.0;  // wait this long after lift before judging delivery
constexpr double kMatchGraceMs  = 150.0;  // pointer input may trail the raw report slightly
constexpr size_t kMaxTracks     = 64;
constexpr size_t kMaxUndelivered = 4096;
constexpr double kPenNearMs     = 500.0;  // touch is held back this close to pen input
constexpr double kHoverStaleMs  = 200.0;  // a hovering pen that stops reporting has left
constexpr double kHoverGapMs    = 500.0;  // longer between hover reports is not an interval

SampleExtras TouchExtras(const POINTER_TOUCH_INFO& ti)
{
    SampleExtras ex;
    if (ti.touchMask & TOUCH_MASK_PRESSURE) ex.pressure = (float)ti.pressure / 1024.0f;
    if (ti.touchMask & TOUCH_MASK_CONTACTAREA)
    {
        ex.cw = (float)(ti.rcContact.right - ti.rcContact.left);
        ex.ch = (float)(ti.rcContact.bottom - ti.rcContact.top);
    }
    if (ti.touchMask & TOUCH_MASK_ORIENTATION) ex.orient = (float)ti.orientation;
    return ex;
}

SampleExtras PenExtras(const POINTER_PEN_INFO& pen)
{
    SampleExtras ex;
    if (pen.penMask & PEN_MASK_PRESSURE) ex.pressure = (float)std::min<UINT32>(pen.pressure, 1024) / 1024.0f;
    if (pen.penMask & PEN_MASK_ROTATION) ex.rotation = (float)pen.rotation;
    if (pen.penMask & PEN_MASK_TILT_X) ex.tiltX = (int8_t)std::clamp<INT32>(pen.tiltX, -90, 90);
    if (pen.penMask & PEN_MASK_TILT_Y) ex.tiltY = (int8_t)std::clamp<INT32>(pen.tiltY, -90, 90);
    ex.penFlags = (uint8_t)(pen.penFlags & (PEN_FLAG_BARREL | PEN_FLAG_INVERTED | PEN_FLAG_ERASER));
    return ex;
}

double EdgeDistance(const RECT& r, float x, float y)
{
    double d = (double)x - r.left;
    d = std::min(d, (double)(r.right - 1) - x);
    d = std::min(d, (double)y - r.top);
    d = std::min(d, (double)(r.bottom - 1) - y);
    return std::max(0.0, d);
}

} // namespace

void Tracker::HandleHidReport(const std::vector<HidContactSample>& contacts,
                              const HidReportInfo& info, int64_t now)
{
    ++m_hidReports;
    m_hidRate.Tick(now);
    // A panel with more contacts than its report has slots sends the rest in
    // continuation reports; the scan is the frame they make up together.
    if (info.frameStart) ++m_hidFrames;
    else ++m_hidSplitReports;
    m_hidMaxContactCount = std::max(m_hidMaxContactCount, info.contactCount);
    switch (info.kind)
    {
    case HRK_TOUCH: ++m_hidTouchReports; break;
    case HRK_NOTIP: ++m_hidNoTipReports; break;
    default:        ++m_hidEmptyReports; break;
    }
    if (info.haveDisplay) { m_hidDisplay = info.display; m_hidHaveDisplay = true; }

    for (const HidContactSample& c : contacts)
    {
        HidTrack* t = nullptr;
        for (HidTrack& tr : m_hidTracks)
            if (!tr.ended && tr.id == c.id) { t = &tr; break; }

        if (!c.tip)
        {
            if (t) { t->ended = true; t->endQpc = now; }
            continue;
        }

        if (!t)
        {
            const bool offWindow = !StartsInWindow(c, info);
            if (m_hidTracks.size() >= kMaxTracks)
            {
                // Something is churning contact ids; settle the oldest now.
                FinalizeHidTrack(m_hidTracks.front());
                m_hidTracks.erase(m_hidTracks.begin());
            }
            m_hidTracks.push_back(HidTrack{});
            t = &m_hidTracks.back();
            t->id = c.id;
            t->mapped = c.mapped;
            t->offWindow = offWindow;
            t->firstQpc = now;
            t->firstX = c.sx;
            t->firstY = c.sy;
            t->edgeSwipeBlocked = m_edgeSwipeBlocked;
        }

        t->sx = c.sx;
        t->sy = c.sy;
        t->lastQpc = now;
        ++t->reports;
        if (c.mapped && info.haveDisplay)
            t->edgeDistPx = std::min(t->edgeDistPx, EdgeDistance(info.display, c.sx, c.sy));

        if (!t->delivered && !t->offWindow) MatchHidToPointers(*t);
    }
}

// Pointer input goes to the window under the finger, and a touch on the frame
// or title bar arrives as non-client input, so the app can only be expected to
// receive contacts that start in its client area. In full screen that area is
// the whole display, edges included.
bool Tracker::StartsInWindow(const HidContactSample& c, const HidReportInfo& info) const
{
    if (!m_hwnd || !c.mapped) return true;   // nothing to test against: judge it

    // The far edge of the logical range maps one pixel past the display, and
    // firmware can overshoot it; test the edge pixel, not whatever lies beyond.
    float x = c.sx, y = c.sy;
    if (info.haveDisplay)
    {
        x = std::max(x, (float)info.display.left);
        x = std::min(x, (float)(info.display.right - 1));
        y = std::max(y, (float)info.display.top);
        y = std::min(y, (float)(info.display.bottom - 1));
    }
    return m_hitTest(m_hwnd, POINT{ (LONG)std::floor(x), (LONG)std::floor(y) });
}

bool Tracker::HitTestClient(HWND hwnd, POINT screenPt)
{
    HWND hit = WindowFromPoint(screenPt);
    if (!hit || GetAncestor(hit, GA_ROOT) != hwnd) return false;
    RECT rc{};
    POINT pt = screenPt;
    return GetClientRect(hwnd, &rc) && ScreenToClient(hwnd, &pt) && PtInRect(&rc, pt);
}

// Matching runs from both sides because raw input and pointer messages for the
// same report arrive in either order; whichever lands second makes the match.
void Tracker::MatchHidToPointers(HidTrack& t)
{
    const int64_t grace = MsToQpc(kMatchGraceMs);
    for (const Contact& pc : m_slots)
    {
        // Raw reports are handled in batches, so a quick tap can be down and
        // up again before its report is; a contact released moments ago counts.
        const bool recent = pc.everUsed && pc.upQpc && t.lastQpc - pc.upQpc <= grace;
        if (!pc.active && !recent) continue;
        if (!t.mapped) { t.delivered = true; return; }   // no mapping: timing only
        const double dx = (double)pc.rawX - t.sx, dy = (double)pc.rawY - t.sy;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d <= kMatchRadiusPx)
        {
            t.delivered = true;
            m_hidMatchOffset.Add(d);
            return;
        }
    }
}

void Tracker::MatchPointerToHid(float screenX, float screenY, int64_t hostQpc)
{
    const int64_t grace = MsToQpc(kMatchGraceMs);
    HidTrack* best = nullptr;
    HidTrack* unmapped = nullptr;
    double bestD = 1e18;

    for (HidTrack& t : m_hidTracks)
    {
        // A contact that started on another window belongs to that window for
        // its whole life, so none of this window's input can be its delivery.
        if (t.offWindow) continue;
        // Bound by the last report, not by now: a contact that stopped
        // reporting but has not been swept yet must not absorb later input.
        const int64_t end = t.ended ? t.endQpc : t.lastQpc;
        if (hostQpc < t.firstQpc - grace || hostQpc > end + grace) continue;
        if (!t.mapped) { unmapped = &t; continue; }
        const double dx = (double)t.sx - screenX, dy = (double)t.sy - screenY;
        const double d = dx * dx + dy * dy;
        if (d < bestD) { bestD = d; best = &t; }
    }

    if (best && bestD <= kMatchRadiusPx * kMatchRadiusPx)
    {
        if (!best->delivered) m_hidMatchOffset.Add(std::sqrt(bestD));
        best->delivered = true;
    }
    else if (!best && unmapped)
    {
        unmapped->delivered = true;
    }
}

// From shortly before the contact started until now: Windows keeps touch back
// for a moment after the pen goes, and a pen that arrives cancels a touch in
// progress.
bool Tracker::PenExcuses(const HidTrack& t) const
{
    return m_penSeenQpc && m_penSeenQpc >= t.firstQpc - MsToQpc(kPenNearMs);
}

void Tracker::FinalizeHidTrack(const HidTrack& t)
{
    if (t.offWindow)
    {
        ++m_hidOffWindow;
        if (t.edgeSwipeBlocked) ++m_hidOffWindowBlocked;
        return;
    }
    if (!t.delivered && PenExcuses(t))
    {
        ++m_hidPenHeld;
        return;
    }

    ++m_hidContacts;
    if (t.delivered)
    {
        ++m_hidDelivered;
        if (t.mapped && t.edgeDistPx < 1e8) m_deliveredEdge.Add(t.edgeDistPx);
        return;
    }

    ++m_hidUndelivered;
    if (t.mapped && t.edgeDistPx < 1e8) m_undeliveredEdge.Add(t.edgeDistPx);
    if (m_undelivered.size() >= kMaxUndelivered) { ++m_undeliveredDropped; return; }

    UndeliveredTouch u;
    u.qpc = t.firstQpc;
    u.x = t.firstX; u.y = t.firstY;
    u.lastX = t.sx; u.lastY = t.sy;
    u.durationMs = QpcToMs((t.ended ? t.endQpc : t.lastQpc) - t.firstQpc);
    u.reports = t.reports;
    u.edgeDistPx = t.edgeDistPx < 1e8 ? t.edgeDistPx : 0.0;
    u.mapped = t.mapped;
    u.edgeSwipeBlocked = t.edgeSwipeBlocked;
    m_undelivered.push_back(u);
}

int Tracker::IngestFrames(uint32_t pointerId, int64_t hostQpc, bool isUp)
{
    UINT32 entries = 0, pcount = 0;
    bool haveHistory = false;

    if (m_useHistory &&
        GetPointerFrameTouchInfoHistory(pointerId, &entries, &pcount, nullptr) &&
        entries > 0 && pcount > 0)
    {
        if (entries > 512) entries = 512;
        if (pcount > (UINT32)kMaxSlots) pcount = (UINT32)kMaxSlots;
        size_t need = (size_t)entries * pcount;
        if (m_scratch.size() < need) m_scratch.resize(need);
        if (GetPointerFrameTouchInfoHistory(pointerId, &entries, &pcount, m_scratch.data()))
            haveHistory = entries > 0 && pcount > 0;
    }

    int accepted = 0;

    if (haveHistory)
    {
        // The history is newest-first; replay it oldest-first so intervals and
        // path order come out in true chronological sequence.
        for (int e = (int)entries - 1; e >= 0; --e)
        {
            const POINTER_TOUCH_INFO* frame = &m_scratch[(size_t)e * pcount];
            uint32_t fid = frame[0].pointerInfo.frameId;

            if (m_haveFrameId && (int32_t)(fid - m_maxFrameId) <= 0)
                continue;                       // already counted this frame
            m_maxFrameId = fid;
            m_haveFrameId = true;

            int64_t devQpc = (int64_t)frame[0].pointerInfo.PerformanceCount;
            if (devQpc <= 0) devQpc = hostQpc;   // device clock unavailable

            ++m_totalFrames;
            m_frameRate.Tick(hostQpc);

            AddFrameInterval(devQpc);

            const bool newest = (e == 0);
            for (UINT32 p = 0; p < pcount; ++p)
                AcceptSample(frame[p], hostQpc, !newest, isUp && newest);
            accepted += (int)pcount;

            m_lastFrameContacts = pcount;
            EndFrame(devQpc);
        }
    }
    else
    {
        // No history available: read the current frame only.
        UINT32 n = (UINT32)kMaxSlots;
        if (m_scratch.size() < n) m_scratch.resize(n);
        if (!GetPointerFrameTouchInfo(pointerId, &n, m_scratch.data()) || n == 0)
        {
            POINTER_TOUCH_INFO ti{};
            if (!GetPointerTouchInfo(pointerId, &ti)) return 0;
            m_scratch[0] = ti; n = 1;
        }

        uint32_t fid = m_scratch[0].pointerInfo.frameId;
        bool fresh = !m_haveFrameId || (int32_t)(fid - m_maxFrameId) > 0;
        if (fresh)
        {
            m_maxFrameId = fid;
            m_haveFrameId = true;
            int64_t devQpc = (int64_t)m_scratch[0].pointerInfo.PerformanceCount;
            if (devQpc <= 0) devQpc = hostQpc;

            ++m_totalFrames;
            m_frameRate.Tick(hostQpc);
            AddFrameInterval(devQpc);

            for (UINT32 p = 0; p < n; ++p)
                AcceptSample(m_scratch[p], hostQpc, false, isUp);
            accepted += (int)n;

            m_lastFrameContacts = n;
            EndFrame(devQpc);
        }
        else
        {
            for (UINT32 p = 0; p < n; ++p)
                AcceptSample(m_scratch[p], hostQpc, false, isUp);
            accepted += (int)n;
        }
    }

    RecountLive(hostQpc);
    return accepted;
}

// A pen is one pointer, so its history holds one sample per input frame.
int Tracker::IngestPenFrames(uint32_t pointerId, int64_t hostQpc, bool isUp)
{
    UINT32 n = 0;
    if (m_useHistory && GetPointerPenInfoHistory(pointerId, &n, nullptr) && n > 0)
    {
        n = std::min<UINT32>(n, 512);
        if (m_penScratch.size() < n) m_penScratch.resize(n);
        if (!GetPointerPenInfoHistory(pointerId, &n, m_penScratch.data())) n = 0;
    }
    else n = 0;
    if (n == 0)
    {
        if (m_penScratch.empty()) m_penScratch.resize(1);
        if (!GetPointerPenInfo(pointerId, m_penScratch.data())) return 0;
        n = 1;
    }
    return HandlePenHistory(m_penScratch.data(), n, hostQpc, isUp);
}

// A pen reports while it hovers as well as while it touches. Only the frames
// with the tip down are measured, as a finger's are; a hover frame moves the
// hover position and times the rate in the air.
int Tracker::HandlePenHistory(const POINTER_PEN_INFO* newestFirst, uint32_t n, int64_t hostQpc, bool isUp)
{
    int accepted = 0;
    for (int e = (int)n - 1; e >= 0; --e)
    {
        const POINTER_PEN_INFO& pen = newestFirst[e];
        const POINTER_INFO& pi = pen.pointerInfo;
        const uint32_t fid = pi.frameId;
        if (m_haveFrameId && (int32_t)(fid - m_maxFrameId) <= 0)
            continue;                       // already counted this frame
        m_maxFrameId = fid;
        m_haveFrameId = true;

        int64_t devQpc = (int64_t)pi.PerformanceCount;
        if (devQpc <= 0) devQpc = hostQpc;
        const bool newest = (e == 0);

        // Tip up with no stroke in progress: hovering. With a stroke still
        // open it is the lift, even if the up flag itself went missing.
        const bool tip = (pi.pointerFlags & (POINTER_FLAG_INCONTACT | POINTER_FLAG_DOWN)) != 0;
        const bool stroke = FindSlot(pi.pointerId) >= 0;
        if (!tip && !stroke)
        {
            AcceptHover(pen, devQpc, hostQpc);
            continue;
        }

        ++m_totalFrames;
        m_frameRate.Tick(hostQpc);
        AddFrameInterval(devQpc);
        AcceptSample(pi, PenExtras(pen), hostQpc, !newest, !tip || (isUp && newest));
        ++accepted;
        m_lastFrameContacts = 1;
        EndFrame(devQpc);
        m_lastHoverDevQpc = 0;              // a hover interval never spans a stroke
        m_lastPenQpc = hostQpc;
    }
    RecountLive(hostQpc);
    return accepted;
}

void Tracker::AcceptHover(const POINTER_PEN_INFO& pen, int64_t devQpc, int64_t hostQpc)
{
    const POINTER_INFO& pi = pen.pointerInfo;
    const SampleExtras ex = PenExtras(pen);
    ++m_hoverReports;
    m_hoverRate.Tick(hostQpc);
    if (m_lastHoverDevQpc)
    {
        const double ms = QpcToMs(devQpc - m_lastHoverDevQpc);
        if (ms > 0.0 && ms < kHoverGapMs)
        {
            m_hoverIntervalMs.Add(ms);
            m_hoverHist.Add(ms);
        }
    }
    m_lastHoverDevQpc = devQpc;

    m_hover.pointerId = pi.pointerId;
    m_hover.x = (float)(pi.ptPixelLocationRaw.x - m_clientOrigin.x);
    m_hover.y = (float)(pi.ptPixelLocationRaw.y - m_clientOrigin.y);
    m_hover.tiltX = ex.tiltX;
    m_hover.tiltY = ex.tiltY;
    m_hover.penFlags = ex.penFlags;
    m_hover.hostQpc = hostQpc;
    m_penFlagsSeen |= ex.penFlags;
    // Names the pen in the header before it has drawn anything.
    m_lastSourceDevice = pi.sourceDevice;
    m_lastPointerType = pi.pointerType;
}

void Tracker::PointerLeft(uint32_t pointerId)
{
    if (m_hover.hostQpc && m_hover.pointerId == pointerId) m_hover.hostQpc = 0;
    m_lastHoverDevQpc = 0;
}

void Tracker::CountHidReport(int64_t now)
{
    ++m_hidReports;
    m_hidRate.Tick(now);
}

bool Tracker::Hovering(int64_t now) const
{
    return m_hover.hostQpc && m_contactsNow == 0 && now - m_hover.hostQpc < MsToQpc(kHoverStaleMs);
}

// Live contacts from slot state, and the touching time and contact-count
// records that depend on it.
void Tracker::RecountLive(int64_t hostQpc)
{
    int live = 0;
    for (const Contact& c : m_slots) if (c.active) ++live;
    if (live != m_contactsNow)
    {
        if (m_contactsNow == 0 && live > 0) m_touchingSince = hostQpc;
        if (live == 0 && m_touchingSince) { m_touchingQpc += hostQpc - m_touchingSince; m_touchingSince = 0; }
        m_contactsNow = live;
    }
    if (live > m_maxSimultaneous) m_maxSimultaneous = live;
    if (live >= 1 && live <= kMaxSlots) m_simSeen[live] = true;
}

// Record the gap to the previous frame. m_lastFrameQpc is cleared whenever the
// screen goes untouched, so an interval never spans the pause between two
// separate touches - that pause is the user's, not the digitizer's.
void Tracker::AddFrameInterval(int64_t devQpc)
{
    if (!m_lastFrameQpc) return;
    double ms = QpcToMs(devQpc - m_lastFrameQpc);
    if (ms <= 0.0 || ms >= 2000.0) return;
    m_intervalMs.Add(ms);
    m_intervalHist.Add(ms);
    m_hzRing.Push((float)(1000.0 / ms));
    if (ms > m_maxGapMs) m_maxGapMs = ms;

    // This runs before the current frame's contacts are applied, so
    // m_lastFrameContacts still holds the count the device was tracking while
    // the interval actually elapsed.
    int n = (int)m_lastFrameContacts;
    if (n >= 1 && n <= kMaxSlots)
    {
        m_intervalByCount[n].Add(ms);
        m_intervalHistByCount[n].Add(ms);
        if (ms > m_maxGapByCount[n]) m_maxGapByCount[n] = ms;
    }
}

void Tracker::EndFrame(int64_t devQpc)
{
    int live = 0;
    for (const Contact& c : m_slots) if (c.active) ++live;
    m_lastFrameQpc = live > 0 ? devQpc : 0;
}

void Tracker::AcceptSample(const POINTER_TOUCH_INFO& ti, int64_t hostQpc,
                           bool fromHistory, bool isUpFrame)
{
    AcceptSample(ti.pointerInfo, TouchExtras(ti), hostQpc, fromHistory, isUpFrame);
}

void Tracker::AcceptSample(const POINTER_INFO& pi, const SampleExtras& ex, int64_t hostQpc,
                           bool fromHistory, bool isUpFrame)
{
    const uint32_t pid = pi.pointerId;
    const bool flagDown = (pi.pointerFlags & POINTER_FLAG_DOWN) != 0;
    const bool flagUp = (pi.pointerFlags & POINTER_FLAG_UP) != 0;
    const bool inContact = (pi.pointerFlags & POINTER_FLAG_INCONTACT) != 0;

    uint8_t kind = SK_UPDATE;
    if (flagDown) kind = SK_DOWN;
    else if (flagUp || (isUpFrame && !inContact)) kind = SK_UP;

    int slot = FindSlot(pid);
    if (slot < 0)
    {
        if (kind == SK_UP) return;      // up for a contact we never saw
        // A hovering pen reports position without touching; it is not a contact.
        if (!inContact && !flagDown) return;
        slot = AssignSlot(pid);
        if (slot < 0) return;           // more contacts than we track
        kind = (kind == SK_UPDATE) ? SK_DOWN : kind;
    }
    Contact& c = m_slots[slot];

    int64_t devQpc = (int64_t)pi.PerformanceCount;
    if (devQpc <= 0) devQpc = hostQpc;

    // Client-space position. The pointer stack reports screen pixels; himetric
    // is kept verbatim because it is the finest-grained value available.
    float cx = (float)(pi.ptPixelLocationRaw.x - m_clientOrigin.x);
    float cy = (float)(pi.ptPixelLocationRaw.y - m_clientOrigin.y);

    bool started = false;
    const double dtMs = AdvanceContact(c, kind, devQpc, cx, cy, started);
    if (started && !fromHistory) c.downLatencyMs = QpcToMs(hostQpc - devQpc);

    c.pointerId = pid;
    c.lastHostQpc = hostQpc;
    c.pxX = pi.ptPixelLocation.x;  c.pxY = pi.ptPixelLocation.y;
    c.rawX = pi.ptPixelLocationRaw.x; c.rawY = pi.ptPixelLocationRaw.y;
    c.hmX = pi.ptHimetricLocationRaw.x; c.hmY = pi.ptHimetricLocationRaw.y;
    c.pointerType = (uint8_t)(pi.pointerType == PT_PEN ? PT_PEN : PT_TOUCH);
    ++c.samples;
    c.rate.Tick(hostQpc);

    c.pressure = ex.pressure; c.cw = ex.cw; c.ch = ex.ch; c.orient = ex.orient;
    c.rotation = ex.rotation; c.tiltX = ex.tiltX; c.tiltY = ex.tiltY; c.penFlags = ex.penFlags;
    if (IsPen()) RecordPen(ex, kind, started);

    // Latency is only meaningful for the sample that triggered this message;
    // older history entries were already sitting in the queue.
    float latency = 0;
    if (!fromHistory && pi.PerformanceCount)
    {
        latency = (float)QpcToMs(hostQpc - devQpc);
        if (latency >= 0 && latency < 500.0f)
        {
            m_latencyMs.Add(latency);
            m_latencyHist.Add(latency);
        }
    }

    // Distance between the processed and unprocessed positions exposes any
    // smoothing or prediction the OS applies on the way to the app.
    double pdx = (double)pi.ptPixelLocation.x - (double)pi.ptPixelLocationRaw.x;
    double pdy = (double)pi.ptPixelLocation.y - (double)pi.ptPixelLocationRaw.y;
    m_predictPx.Add(std::sqrt(pdx * pdx + pdy * pdy));

    if (!m_hmSeen)
    {
        m_hmSeen = true;
        m_hmMinX = m_hmMaxX = c.hmX;
        m_hmMinY = m_hmMaxY = c.hmY;
    }
    else
    {
        m_hmMinX = std::min(m_hmMinX, c.hmX); m_hmMaxX = std::max(m_hmMaxX, c.hmX);
        m_hmMinY = std::min(m_hmMinY, c.hmY); m_hmMaxY = std::max(m_hmMaxY, c.hmY);
    }

    ++m_totalSamples;
    if (fromHistory) ++m_historySamples;
    m_sampleRate.Tick(hostQpc);

    c.PushTrail(cx, cy, devQpc, ex.pressure, fromHistory ? 1 : 0);
    PushInk(cx, cy, slot, fromHistory, ex.pressure);

    ExportRec r;
    r.deviceQpc = devQpc;
    r.hostQpc = hostQpc;
    r.pointerId = pid;
    r.frameId = pi.frameId;
    r.x = cx; r.y = cy;
    r.pxX = pi.ptPixelLocation.x;  r.pxY = pi.ptPixelLocation.y;
    r.rawX = pi.ptPixelLocationRaw.x; r.rawY = pi.ptPixelLocationRaw.y;
    r.hmX = pi.ptHimetricLocationRaw.x; r.hmY = pi.ptHimetricLocationRaw.y;
    r.pressure = ex.pressure; r.cw = ex.cw; r.ch = ex.ch; r.orient = ex.orient;
    r.rotation = ex.rotation; r.tiltX = ex.tiltX; r.tiltY = ex.tiltY; r.penFlags = ex.penFlags;
    r.dtMs = (float)dtMs;
    r.latencyMs = latency;
    r.slot = (uint8_t)slot;
    r.kind = kind;
    r.fromHistory = fromHistory ? 1 : 0;
    r.pointerType = c.pointerType;
    PushRecord(r);

    m_lastSourceDevice = pi.sourceDevice;
    m_lastPointerType = pi.pointerType;

    // Credit the panel contact this pointer sample corresponds to.
    if (!m_hidTracks.empty())
        MatchPointerToHid((float)pi.ptPixelLocationRaw.x, (float)pi.ptPixelLocationRaw.y, hostQpc);

    if (kind == SK_UP) ReleaseSlot(slot, devQpc);
}

// The pen's pressure, tilt and buttons from one sample of a stroke. The lift
// reads 0 and is left out; the break it makes is marked when the slot is freed.
void Tracker::RecordPen(const SampleExtras& ex, uint8_t kind, bool started)
{
    m_penFlagsSeen |= ex.penFlags;
    if (kind == SK_UP) return;
    if (ex.pressure >= 0)
    {
        m_pressure.Add(ex.pressure);
        if (started) m_pressureAtDown.Add(ex.pressure);
        const long level = std::lround(ex.pressure * 1024.0);
        if (level >= 0 && (size_t)level < m_levelSeen.size() && !m_levelSeen[(size_t)level])
        {
            m_levelSeen[(size_t)level] = 1;
            ++m_pressureLevels;
        }
        m_pressureRing.Push(ex.pressure);
    }
    if (ex.tiltX != kNoTilt) m_tiltX.Add(ex.tiltX);
    if (ex.tiltY != kNoTilt) m_tiltY.Add(ex.tiltY);
    if (ex.rotation >= 0) m_rotationSeen = true;
}

// Interval, stroke start and path for one sample of a contact - the part
// touch screen and touch pad samples share. Returns the interval to the
// contact's previous sample (0 for its first); 'started' is set when the
// sample begins a new stroke.
double Tracker::AdvanceContact(Contact& c, uint8_t kind, int64_t devQpc, float x, float y, bool& started)
{
    double dtMs = 0;
    if (c.samples && c.lastDeviceQpc)
    {
        dtMs = QpcToMs(devQpc - c.lastDeviceQpc);
        if (dtMs > 0.0 && dtMs < 2000.0)
        {
            c.dt.Add(dtMs);
            c.instHz = 1000.0 / dtMs;
        }
    }

    // A repeated down flag for a contact already being tracked is the same
    // press, not a new one, so the stroke is only started once.
    started = false;
    if (kind == SK_DOWN && !c.downCounted)
    {
        started = true;
        c.downCounted = true;
        c.downQpc = devQpc;
        c.downX = x; c.downY = y;
        c.ClearTrail();
        c.dt.Reset();
        c.samples = 0;
        c.pathLenPx = 0;
        ++m_downs;
        if (m_lastDownQpc)
        {
            double gap = QpcToMs(devQpc - m_lastDownQpc);
            if (gap > 0 && gap < 60000.0) m_tapIntervalMs.Add(gap);
        }
        m_lastDownQpc = devQpc;
    }
    else if (c.samples)
    {
        float dx = x - c.x, dy = y - c.y;
        c.pathLenPx += std::sqrt((double)(dx * dx + dy * dy));
    }

    c.lastDeviceQpc = devQpc;
    c.x = x; c.y = y;
    return dtMs;
}

void Tracker::PushInk(float x, float y, int slot, bool fromHistory, float pressure)
{
    if (m_ink.empty()) return;
    m_ink[m_inkHead] = InkPt{ x, y, (uint8_t)slot, (uint8_t)(fromHistory ? 1 : 0),
                              (uint8_t)std::lround(Clampd(pressure, 0.0, 1.0) * 255.0) };
    m_inkHead = (m_inkHead + 1) % m_ink.size();
    if (m_inkCount < m_ink.size()) ++m_inkCount;
    ++m_inkTotal;
}

// ------------------------------------------------------------- touch pad

void Tracker::HandlePadFrame(const std::vector<PadContact>& contacts, int64_t devQpc,
                             int64_t hostQpc, uint32_t frameId)
{
    ++m_totalFrames;
    m_frameRate.Tick(hostQpc);
    AddFrameInterval(devQpc);

    // A pad reports every contact in every frame, so one that is missing has
    // gone without its final report - typically a finger the pad re-classed
    // as a palm.
    for (int i = 0; i < kMaxSlots; ++i)
    {
        if (!m_slots[i].active) continue;
        bool present = false;
        for (const PadContact& pc : contacts)
            if (pc.id == m_slots[i].pointerId) { present = true; break; }
        if (!present) ReleaseSlot(i, devQpc);
    }

    for (const PadContact& pc : contacts)
        AcceptPadContact(pc, devQpc, hostQpc, frameId);

    m_lastFrameContacts = (uint32_t)contacts.size();
    EndFrame(devQpc);
    RecountLive(hostQpc);
}

void Tracker::AcceptPadContact(const PadContact& pc, int64_t devQpc, int64_t hostQpc, uint32_t frameId)
{
    uint8_t kind = pc.tip ? SK_UPDATE : SK_UP;
    int slot = FindSlot(pc.id);
    if (slot < 0)
    {
        if (!pc.tip) return;            // a lift for a contact never seen down
        slot = AssignSlot(pc.id);
        if (slot < 0) return;
        kind = SK_DOWN;
    }
    Contact& c = m_slots[slot];

    bool started = false;
    const double dtMs = AdvanceContact(c, kind, devQpc, pc.x, pc.y, started);
    c.lastHostQpc = hostQpc;
    c.pointerType = (uint8_t)PT_TOUCHPAD;
    c.pressure = c.cw = c.ch = c.orient = -1;
    ++c.samples;
    c.rate.Tick(hostQpc);

    ++m_totalSamples;
    m_sampleRate.Tick(hostQpc);
    // Pad positions are not screen positions, so they stay out of the ink.
    c.PushTrail(pc.x, pc.y, devQpc, -1, 0);

    ExportRec r;
    r.deviceQpc = devQpc;
    r.hostQpc = hostQpc;
    r.pointerId = pc.id;
    r.frameId = frameId;
    r.x = pc.x; r.y = pc.y;
    r.dtMs = (float)dtMs;
    r.slot = (uint8_t)slot;
    r.kind = kind;
    r.pointerType = (uint8_t)PT_TOUCHPAD;
    PushRecord(r);

    if (kind == SK_UP) ReleaseSlot(slot, devQpc);
}

void Tracker::Update(int64_t now)
{
    bool released = false;
    for (int i = 0; i < kMaxSlots; ++i)
    {
        Contact& c = m_slots[i];
        if (!c.active) continue;

        // Fade the per-contact instantaneous rate when a finger stops reporting
        // so a stale number cannot be mistaken for a live one.
        double idleMs = c.lastHostQpc ? QpcToMs(now - c.lastHostQpc) : 0.0;
        if (idleMs > 250.0) c.instHz = 0;

        // Watchdog: a lost WM_POINTERUP (capture change, device unplug, window
        // losing the contact) would otherwise pin a contact down forever.
        if (c.lastHostQpc && idleMs > 2000.0)
        {
            ReleaseSlot(i, now, false);
            released = true;
        }
    }

    if (released)
    {
        int live = 0;
        for (const Contact& c : m_slots) if (c.active) ++live;
        if (live == 0 && m_touchingSince) { m_touchingQpc += now - m_touchingSince; m_touchingSince = 0; }
        m_contactsNow = live;
    }
    if (m_contactsNow > 0 && m_touchingSince == 0) m_touchingSince = now;

    // Panel contacts: end the ones that stopped reporting, then judge delivery
    // once enough time has passed for any trailing pointer input to arrive.
    const int64_t liftTimeout = MsToQpc(kLiftTimeoutMs);
    const int64_t settle = MsToQpc(kSettleMs);
    for (HidTrack& t : m_hidTracks)
        if (!t.ended && now - t.lastQpc > liftTimeout) { t.ended = true; t.endQpc = t.lastQpc; }

    for (size_t i = 0; i < m_hidTracks.size();)
    {
        if (m_hidTracks[i].ended && now - m_hidTracks[i].endQpc > settle)
        {
            FinalizeHidTrack(m_hidTracks[i]);
            m_hidTracks.erase(m_hidTracks.begin() + (ptrdiff_t)i);
        }
        else ++i;
    }
}

// The report rate, from the centre of the interval peak: the tallest step and
// every neighbouring one that belongs with it, weighted by the samples in each,
// stopping at a separate cluster such as missed reports.
//
// A digitizer whose reports land on a coarse tick alternates between two
// intervals - a 95.5 Hz panel on a 1 ms USB frame clock reads as 10 and 11 ms,
// a 134 Hz pad on a 1 ms clock as 7 and 8 - and the tallest step alone names
// neither rate.
static double PeakCentreHz(const Histogram& h, double reachMs)
{
    const int m = h.ModeBin();
    if (m < 0) return 0;
    const int reach = (int)std::ceil(reachMs / h.binW);
    int lo = m, hi = m;
    for (int i = m - 1, gap = 0; i >= 0 && gap <= reach; --i)
    {
        if (h.bins[(size_t)i]) { lo = i; gap = 0; } else ++gap;
    }
    for (int i = m + 1, gap = 0; i < h.nbins && gap <= reach; ++i)
    {
        if (h.bins[(size_t)i]) { hi = i; gap = 0; } else ++gap;
    }
    double n = 0, sum = 0;
    for (int i = lo; i <= hi; ++i) { n += (double)h.bins[(size_t)i]; sum += h.sums[(size_t)i]; }
    return sum > 0 ? 1000.0 * n / sum : 0;
}

// How far the peak may reach: a whole step of the clock the intervals are
// timed on, and in any case a quarter of the interval itself, which spans the
// neighbouring ticks without reaching a missed report at twice the interval.
static double PeakReachMs(const Histogram& h, double clockResMs)
{
    const int m = h.ModeBin();
    const double ms = m >= 0 ? h.BinCenter(m) : 0;
    return std::max(clockResMs + h.binW, ms * 0.25);
}

// The most common gap: the mean of the values in the tallest step, which is
// exact where the step's centre would be off by up to half a step.
static double ModalMs(const Histogram& h)
{
    const int b = h.ModeBin();
    if (b < 0) return 0;
    const uint64_t n = h.bins[(size_t)b];
    return n ? h.sums[(size_t)b] / (double)n : h.BinCenter(b);
}

double Tracker::ModeMs() const { return ModalMs(m_intervalHist); }

double Tracker::ModeHz() const
{
    const double ms = ModeMs();
    return ms > 0 ? 1000.0 / ms : 0;
}

double Tracker::RateHz() const
{
    return PeakCentreHz(m_intervalHist, PeakReachMs(m_intervalHist, m_clockResMs));
}

double Tracker::HoverRateHz() const
{
    return PeakCentreHz(m_hoverHist, PeakReachMs(m_hoverHist, m_clockResMs));
}

// ------------------------------------------------------- rate by contact count

static int ClampCount(int n) { return (n < 0 || n > kMaxSlots) ? 0 : n; }

double Tracker::RateHzAt(int contacts) const
{
    const Histogram& h = m_intervalHistByCount[ClampCount(contacts)];
    return PeakCentreHz(h, PeakReachMs(h, m_clockResMs));
}

// Intervals one contact count needs before its most common gap means much.
static constexpr uint64_t kGapJudgeMin = 100;

bool Tracker::GapsJudged() const
{
    for (int n = 1; n <= kMaxSlots; ++n)
        if (m_intervalByCount[n].n >= kGapJudgeMin) return true;
    return false;
}

bool Tracker::GapsVary() const
{
    if (CoarseClock()) return false;
    for (int n = 1; n <= kMaxSlots; ++n)
    {
        if (m_intervalByCount[n].n < kGapJudgeMin) continue;
        const double mode = ModeHzAt(n);
        if (mode > 0 && std::fabs(RateHzAt(n) / mode - 1.0) > 0.02) return true;
    }
    return false;
}

const Stats& Tracker::IntervalMsAt(int contacts) const
{
    return m_intervalByCount[ClampCount(contacts)];
}
const Histogram& Tracker::IntervalHistAt(int contacts) const
{
    return m_intervalHistByCount[ClampCount(contacts)];
}
double Tracker::ModeMsAt(int contacts) const
{
    return ModalMs(m_intervalHistByCount[ClampCount(contacts)]);
}
double Tracker::ModeHzAt(int contacts) const
{
    const double ms = ModeMsAt(contacts);
    return ms > 0 ? 1000.0 / ms : 0;
}
double Tracker::MeanHzAt(int contacts) const
{
    const Stats& s = m_intervalByCount[ClampCount(contacts)];
    return s.n && s.mean > 0 ? 1000.0 / s.mean : 0;
}
double Tracker::MaxGapMsAt(int contacts) const
{
    return m_maxGapByCount[ClampCount(contacts)];
}
bool Tracker::HasDataAt(int contacts) const
{
    return m_intervalByCount[ClampCount(contacts)].n > 0;
}

// ---------------------------------------------------------------- export ring

void Tracker::PushRecord(const ExportRec& r)
{
    if (m_recs.empty()) return;
    if (m_recCount == m_recs.size()) ++m_recDropped;
    m_recs[m_recHead] = r;
    m_recHead = (m_recHead + 1) % m_recs.size();
    if (m_recCount < m_recs.size()) ++m_recCount;
    if (!m_log) return;
    if (IsPad()) WritePadLogRow(r);
    else if (IsPen()) WritePenLogRow(r);
    else WriteLogRow(r);
}

// --------------------------------------------------------------- live logging

static const char* kCsvHeader =
    "seq,kind,pointer_id,slot,frame_id,from_history,pointer_type,"
    "device_qpc,device_ms,host_qpc,host_ms,dt_ms,inst_hz,latency_ms,"
    "client_x,client_y,screen_x,screen_y,raw_screen_x,raw_screen_y,"
    "himetric_x,himetric_y,predict_dx,predict_dy,"
    "pressure,contact_w,contact_h,orientation\n";

const char* const kPadCsvHeader =
    "seq,kind,contact_id,slot,frame,device_qpc,device_ms,host_qpc,host_ms,"
    "dt_ms,inst_hz,x,y\n";

// pressure is 0..1; pressure_level is the same as the 0..1024 integer Windows
// passes on, which is what to count distinct levels by.
const char* const kPenCsvHeader =
    "seq,kind,pointer_id,frame_id,from_history,device_qpc,device_ms,host_qpc,host_ms,"
    "dt_ms,inst_hz,latency_ms,client_x,client_y,screen_x,screen_y,raw_screen_x,raw_screen_y,"
    "himetric_x,himetric_y,predict_dx,predict_dy,"
    "pressure,pressure_level,tilt_x,tilt_y,rotation,barrel,inverted,eraser\n";

int FormatPenCsvRow(char* buf, size_t n, uint64_t seq, const ExportRec& r, int64_t t0)
{
    char pr[32] = ",", tx[8] = "", ty[8] = "", rot[16] = "";
    if (r.pressure >= 0) snprintf(pr, sizeof pr, "%.4f,%ld", r.pressure, std::lround(r.pressure * 1024.0));
    if (r.tiltX != kNoTilt) snprintf(tx, sizeof tx, "%d", r.tiltX);
    if (r.tiltY != kNoTilt) snprintf(ty, sizeof ty, "%d", r.tiltY);
    if (r.rotation >= 0) snprintf(rot, sizeof rot, "%.0f", r.rotation);
    const double hz = r.dtMs > 0 ? 1000.0 / r.dtMs : 0.0;
    const int len = snprintf(buf, n,
        "%llu,%s,%u,%u,%u,%lld,%.6f,%lld,%.6f,%.6f,%.3f,%.6f,"
        "%.2f,%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%s,%s,%s,%s,%d,%d,%d\n",
        (unsigned long long)seq, SampleKindName(r.kind), r.pointerId, r.frameId, r.fromHistory,
        (long long)r.deviceQpc, QpcToMs(r.deviceQpc - t0), (long long)r.hostQpc, QpcToMs(r.hostQpc - t0),
        r.dtMs, hz, r.latencyMs, r.x, r.y, r.pxX, r.pxY, r.rawX, r.rawY, r.hmX, r.hmY,
        r.pxX - r.rawX, r.pxY - r.rawY, pr, tx, ty, rot,
        (r.penFlags & PEN_FLAG_BARREL) ? 1 : 0, (r.penFlags & PEN_FLAG_INVERTED) ? 1 : 0,
        (r.penFlags & PEN_FLAG_ERASER) ? 1 : 0);
    return len < 0 ? 0 : std::min(len, (int)n - 1);
}

bool Tracker::StartLiveLog(const std::wstring& path)
{
    StopLiveLog();
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    setvbuf(f, nullptr, _IOFBF, 1 << 20);
    fputs(IsPad() ? kPadCsvHeader : IsPen() ? kPenCsvHeader : kCsvHeader, f);
    m_log = f;
    m_logPath = path;
    m_logRows = 0;
    return true;
}

void Tracker::StopLiveLog()
{
    if (m_log) { fflush(m_log); fclose(m_log); m_log = nullptr; }
}

void Tracker::WriteLogRow(const ExportRec& r)
{
    double devMs = QpcToMs(r.deviceQpc);
    double hostMs = QpcToMs(r.hostQpc);
    double instHz = r.dtMs > 0 ? 1000.0 / r.dtMs : 0.0;
    fprintf(m_log,
        "%llu,%s,%u,%u,%u,%u,%s,"
        "%lld,%.6f,%lld,%.6f,%.6f,%.3f,%.6f,"
        "%.2f,%.2f,%d,%d,%d,%d,"
        "%d,%d,%d,%d,",
        (unsigned long long)m_logRows, SampleKindName(r.kind), r.pointerId, r.slot,
        r.frameId, r.fromHistory, r.pointerType == PT_PEN ? "pen" : "touch",
        (long long)r.deviceQpc, devMs, (long long)r.hostQpc, hostMs, r.dtMs, instHz, r.latencyMs,
        r.x, r.y, r.pxX, r.pxY, r.rawX, r.rawY,
        r.hmX, r.hmY, r.pxX - r.rawX, r.pxY - r.rawY);
    if (r.pressure >= 0) fprintf(m_log, "%.4f,", r.pressure); else fputs(",", m_log);
    if (r.cw >= 0) fprintf(m_log, "%.1f,%.1f,", r.cw, r.ch); else fputs(",,", m_log);
    if (r.orient >= 0) fprintf(m_log, "%.1f\n", r.orient); else fputs("\n", m_log);
    ++m_logRows;
}

void Tracker::WritePenLogRow(const ExportRec& r)
{
    char b[512];
    const int n = FormatPenCsvRow(b, sizeof b, m_logRows, r, 0);
    fwrite(b, 1, (size_t)n, m_log);
    ++m_logRows;
}

void Tracker::WritePadLogRow(const ExportRec& r)
{
    const double instHz = r.dtMs > 0 ? 1000.0 / r.dtMs : 0.0;
    fprintf(m_log, "%llu,%s,%u,%u,%u,%lld,%.6f,%lld,%.6f,%.6f,%.3f,%.1f,%.1f\n",
            (unsigned long long)m_logRows, SampleKindName(r.kind), r.pointerId, r.slot, r.frameId,
            (long long)r.deviceQpc, QpcToMs(r.deviceQpc), (long long)r.hostQpc, QpcToMs(r.hostQpc),
            r.dtMs, instHz, r.x, r.y);
    ++m_logRows;
}
