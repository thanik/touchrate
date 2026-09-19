// TouchRate - touch sample ingestion and measurement
#pragma once
#include "common.h"
#include "hidtouch.h"

enum SampleKind : uint8_t { SK_DOWN = 0, SK_UPDATE = 1, SK_UP = 2 };

constexpr int  kMaxSlots  = 20;
constexpr int  kTrailCap  = 2048;   // live stroke points kept per contact
constexpr size_t kInkCap  = 65536;  // ink points buffered between frames

struct TrailPt
{
    float   x = 0, y = 0;
    int64_t qpc = 0;
    float   pressure = -1;
    uint8_t fromHistory = 0;
};

struct InkPt
{
    float   x = 0, y = 0;
    uint8_t slot = 0;
    uint8_t fromHistory = 0;
};

// One finger in a complete touch pad frame, in pad units. A contact with tip
// false is lifting in this frame.
struct PadContact
{
    uint32_t id = 0;
    bool     tip = false;
    float    x = 0, y = 0;
};

// One row per accepted hardware sample; this is what gets exported.
struct ExportRec
{
    int64_t  deviceQpc = 0, hostQpc = 0;
    uint32_t pointerId = 0, frameId = 0;
    float    x = 0, y = 0;                 // client pixels; pad units for a touch pad
    int32_t  pxX = 0, pxY = 0;             // screen pixels (processed)
    int32_t  rawX = 0, rawY = 0;           // screen pixels (unprocessed)
    int32_t  hmX = 0, hmY = 0;             // himetric, unprocessed
    float    pressure = -1, cw = -1, ch = -1, orient = -1;
    float    dtMs = 0, latencyMs = 0;
    uint8_t  slot = 0, kind = SK_UPDATE, fromHistory = 0, pointerType = 0;
};

// A contact as the panel itself reported it, followed from first touching
// report to lift, and matched against the pointer contacts Windows delivered.
struct HidTrack
{
    uint32_t id = 0;
    bool     ended = false;
    bool     delivered = false;
    bool     mapped = false;
    bool     edgeSwipeBlocked = false;   // blocking was in effect when it started
    bool     offWindow = false;          // started outside the window: not judged
    float    sx = 0, sy = 0;             // latest screen position
    float    firstX = 0, firstY = 0;
    int64_t  firstQpc = 0, lastQpc = 0, endQpc = 0;
    uint32_t reports = 0;
    double   edgeDistPx = 1e9;           // closest approach to the display edge
};

// A panel contact that Windows never turned into pointer input.
struct UndeliveredTouch
{
    int64_t  qpc = 0;
    float    x = 0, y = 0;               // where it started, screen pixels
    float    lastX = 0, lastY = 0;
    double   durationMs = 0;
    uint32_t reports = 0;
    double   edgeDistPx = 0;
    bool     mapped = false;
    bool     edgeSwipeBlocked = false;
};

struct Contact
{
    bool     active = false;
    bool     everUsed = false;
    bool     downCounted = false;   // some drivers repeat POINTER_FLAG_DOWN
    uint32_t pointerId = 0;
    int64_t  downQpc = 0, lastDeviceQpc = 0, lastHostQpc = 0, upQpc = 0;

    float    x = 0, y = 0;                 // client pixels
    float    downX = 0, downY = 0;
    int32_t  pxX = 0, pxY = 0, rawX = 0, rawY = 0, hmX = 0, hmY = 0;
    float    pressure = -1, cw = -1, ch = -1, orient = -1;
    uint8_t  pointerType = 0;

    uint64_t samples = 0;
    double   instHz = 0;
    double   pathLenPx = 0;
    double   downLatencyMs = 0;
    Stats    dt;                           // this stroke's sample intervals
    RateMeter rate;

    TrailPt  trail[kTrailCap];
    size_t   trailCount = 0, trailHead = 0;

    void PushTrail(float tx, float ty, int64_t tqpc, float tpressure, uint8_t hist)
    {
        trail[trailHead] = TrailPt{ tx, ty, tqpc, tpressure, hist };
        trailHead = (trailHead + 1) % kTrailCap;
        if (trailCount < kTrailCap) ++trailCount;
    }
    const TrailPt& TrailAt(size_t i) const   // i = 0 is oldest retained
    {
        size_t start = (trailHead + kTrailCap - trailCount) % kTrailCap;
        return trail[(start + i) % kTrailCap];
    }
    void ClearTrail() { trailCount = 0; trailHead = 0; }
};

// Measures one input source. The touch screen feeds it pointer messages; a
// touch pad, which Windows never delivers as touch, feeds a second instance
// whole frames decoded from its HID reports.
class Tracker
{
public:
    void Init(size_t exportCapacity, bool pad = false);
    bool IsPad() const { return m_pad; }
    void ResetStats();          // clears measurements, keeps live contacts
    void ClearInk();

    void SetWindow(HWND h) { m_hwnd = h; }
    void SetClientOrigin(POINT p) { m_clientOrigin = p; }
    POINT ClientOrigin() const { return m_clientOrigin; }
    void SetUseHistory(bool v) { m_useHistory = v; }
    bool UseHistory() const { return m_useHistory; }

    // Called from the window procedure. Returns accepted sample count.
    int  HandlePointerMessage(UINT msg, WPARAM wParam, int64_t hostQpc);
    // One decoded HID touch report from the digitizer, via Raw Input.
    void HandleHidReport(const std::vector<HidContactSample>& contacts,
                         const HidReportInfo& info, int64_t now);
    // One complete touch pad frame: its finger contacts, the pad's own clock
    // mapped to QPC units, and the arrival time. Contacts absent from the
    // frame have lifted.
    void HandlePadFrame(const std::vector<PadContact>& contacts, int64_t devQpc,
                        int64_t hostQpc, uint32_t frameId);
    void Update(int64_t now);   // per-frame bookkeeping

    // Whether Windows edge-swipe gestures are currently blocked for the window.
    void SetEdgeSwipeBlocked(bool v) { m_edgeSwipeBlocked = v; }

    // ---- live state
    const Contact& Slot(int i) const { return m_slots[i]; }
    int      ContactsNow() const { return m_contactsNow; }
    int      MaxSimultaneous() const { return m_maxSimultaneous; }
    bool     SawSimultaneous(int n) const { return n >= 1 && n <= kMaxSlots ? m_simSeen[n] : false; }
    HANDLE   LastSourceDevice() const { return m_lastSourceDevice; }
    uint32_t LastPointerType() const { return m_lastPointerType; }

    // ---- rates
    double FrameHz(int64_t now) const { return m_frameRate.Hz(now); }
    double SampleHz(int64_t now) const { return m_sampleRate.Hz(now); }
    double MessageHz(int64_t now) const { return m_msgRate.Hz(now); }
    double HidReportHz(int64_t now) const { return m_hidRate.Hz(now); }
    bool   HidSeen() const { return m_hidReports > 0; }

    // ---- delivery: what the panel reported versus what reached the app
    uint64_t HidFrames()        const { return m_hidFrames; }
    uint64_t HidTouchReports()  const { return m_hidTouchReports; }
    uint64_t HidNoTipReports()  const { return m_hidNoTipReports; }
    uint64_t HidEmptyReports()  const { return m_hidEmptyReports; }
    uint64_t HidContacts()      const { return m_hidContacts; }
    uint64_t HidDelivered()     const { return m_hidDelivered; }
    uint64_t HidUndelivered()   const { return m_hidUndelivered; }
    // Panel contacts that started on another window, the desktop, the taskbar
    // or this window's frame. Windows rightly delivered them elsewhere, so they
    // are counted here instead of in HidContacts().
    uint64_t HidOffWindow()     const { return m_hidOffWindow; }
    uint64_t HidOffWindowBlocked() const { return m_hidOffWindowBlocked; }   // with edge swipes blocked
    uint64_t UndeliveredDropped() const { return m_undeliveredDropped; }
    const Stats& HidMatchOffsetPx()    const { return m_hidMatchOffset; }
    const Stats& DeliveredEdgeDist()   const { return m_deliveredEdge; }
    const Stats& UndeliveredEdgeDist() const { return m_undeliveredEdge; }
    const std::vector<UndeliveredTouch>& Undelivered() const { return m_undelivered; }
    size_t UndeliveredMarkStart() const { return m_undeliveredMarkStart; }
    const std::vector<HidTrack>& HidTracks() const { return m_hidTracks; }
    bool HidDisplay(RECT& r) const { r = m_hidDisplay; return m_hidHaveDisplay; }

    // ---- statistics
    const Stats& IntervalMs() const { return m_intervalMs; }
    const Stats& LatencyMs()  const { return m_latencyMs; }
    const Stats& PredictPx()  const { return m_predictPx; }
    const Stats& TapIntervalMs() const { return m_tapIntervalMs; }
    const Stats& DwellMs()    const { return m_dwellMs; }
    const Histogram& IntervalHist() const { return m_intervalHist; }
    const Histogram& LatencyHist()  const { return m_latencyHist; }
    const Ring& HzRing() const { return m_hzRing; }

    double MaxGapMs() const { return m_maxGapMs; }
    double AvgHz()    const { return m_intervalMs.n && m_intervalMs.mean > 0 ? 1000.0 / m_intervalMs.mean : 0.0; }
    double ModeHz()   const;

    // Resolution of the clock the intervals are timed on, when it is coarser
    // than the histogram: a touch pad's scan time may count whole milliseconds.
    // The modal rate is then the centre of the interval peak, not its tallest step.
    void   SetClockResolutionMs(double ms) { m_clockResMs = ms; }
    double ClockResolutionMs() const { return m_clockResMs; }

    // Report rate broken down by how many contacts the digitizer was tracking.
    // Many panels slow down as fingers are added, which is exactly the case a
    // rhythm game hits during dense passages.
    const Stats&     IntervalMsAt(int contacts) const;
    const Histogram& IntervalHistAt(int contacts) const;
    double ModeHzAt(int contacts) const;
    double MeanHzAt(int contacts) const;
    double MaxGapMsAt(int contacts) const;
    bool   HasDataAt(int contacts) const;
    double TouchingSec() const { return QpcToSec(m_touchingQpc); }

    uint64_t TotalSamples() const { return m_totalSamples; }
    uint64_t TotalFrames()  const { return m_totalFrames; }
    uint64_t HistorySamples() const { return m_historySamples; }
    uint64_t Messages()     const { return m_messages; }
    uint64_t HidReports()   const { return m_hidReports; }
    uint64_t Downs()        const { return m_downs; }
    uint64_t Ups()          const { return m_ups; }
    // Contacts that stopped reporting without an up message and were released
    // by the watchdog. A non-zero count means the input stream lost a release.
    uint64_t LostContacts() const { return m_lostContacts; }

    // Observed himetric envelope, useful for judging digitizer resolution.
    bool HimetricSeen() const { return m_hmSeen; }
    void HimetricRange(int32_t& x0, int32_t& y0, int32_t& x1, int32_t& y1) const
    { x0 = m_hmMinX; y0 = m_hmMinY; x1 = m_hmMaxX; y1 = m_hmMaxY; }

    // ---- ink / export buffers
    const std::vector<InkPt>& Ink() const { return m_ink; }
    size_t InkCount() const { return m_inkCount; }
    size_t InkHead()  const { return m_inkHead; }
    // Monotonic count of ink points ever pushed; the UI uses the difference
    // since the last frame to draw only what is new into the ink layer.
    uint64_t InkTotal() const { return m_inkTotal; }

    const std::vector<ExportRec>& Records() const { return m_recs; }
    size_t RecordCount() const { return m_recCount; }
    size_t RecordHead()  const { return m_recHead; }
    size_t RecordCapacity() const { return m_recs.size(); }
    uint64_t RecordsDropped() const { return m_recDropped; }
    const ExportRec& RecordAt(size_t i) const   // i = 0 is oldest retained
    {
        size_t cap = m_recs.size();
        size_t start = (m_recHead + cap - m_recCount) % cap;
        return m_recs[(start + i) % cap];
    }

    // ---- live logging
    bool StartLiveLog(const std::wstring& path);
    void StopLiveLog();
    bool LiveLogging() const { return m_log != nullptr; }
    const std::wstring& LiveLogPath() const { return m_logPath; }
    uint64_t LiveLogRows() const { return m_logRows; }

private:
    int  IngestFrames(uint32_t pointerId, int64_t hostQpc, bool isUp);
    void AcceptSample(const POINTER_TOUCH_INFO& ti, int64_t hostQpc, bool fromHistory, bool isUp);
    void AcceptPadContact(const PadContact& pc, int64_t devQpc, int64_t hostQpc, uint32_t frameId);
    double AdvanceContact(Contact& c, uint8_t kind, int64_t devQpc, float x, float y, bool& started);
    void PushInk(float x, float y, int slot, bool fromHistory);
    void RecountLive(int64_t hostQpc);
    void AddFrameInterval(int64_t devQpc);
    void EndFrame(int64_t devQpc);
    void MatchPointerToHid(float screenX, float screenY, int64_t hostQpc);
    void MatchHidToPointers(HidTrack& t);
    void FinalizeHidTrack(const HidTrack& t);
    bool StartsInWindow(const HidContactSample& c, const HidReportInfo& info) const;
    static bool HitTestClient(HWND hwnd, POINT screenPt);
    int  AssignSlot(uint32_t pointerId);
    int  FindSlot(uint32_t pointerId) const;
    void ReleaseSlot(int slot, int64_t qpc, bool genuine = true);
    void PushRecord(const ExportRec& r);
    void WriteLogRow(const ExportRec& r);
    void WritePadLogRow(const ExportRec& r);

    bool  m_pad = false;
    double m_clockResMs = 0;
    HWND  m_hwnd = nullptr;
    POINT m_clientOrigin{ 0, 0 };
    // Where a panel contact starts is hit-tested through this, so a test can
    // stand in for the desktop's window layout.
    bool (*m_hitTest)(HWND hwnd, POINT screenPt) = &Tracker::HitTestClient;
    bool  m_useHistory = true;

    Contact m_slots[kMaxSlots];
    int     m_contactsNow = 0;
    int     m_maxSimultaneous = 0;
    bool    m_simSeen[kMaxSlots + 1] = {};

    // Frame-level dedup: input frame ids increase monotonically, so a
    // wraparound-safe comparison against the newest id seen is sufficient.
    uint32_t m_maxFrameId = 0;
    bool     m_haveFrameId = false;
    int64_t  m_lastFrameQpc = 0;
    uint32_t m_lastFrameContacts = 0;

    RateMeter m_frameRate, m_sampleRate, m_msgRate, m_hidRate;
    Stats     m_intervalMs, m_latencyMs, m_predictPx, m_tapIntervalMs, m_dwellMs;
    Histogram m_intervalHist, m_latencyHist;
    Ring      m_hzRing;
    double    m_maxGapMs = 0;

    // Indexed by simultaneous contact count, 1..kMaxSlots (slot 0 unused).
    Stats     m_intervalByCount[kMaxSlots + 1];
    Histogram m_intervalHistByCount[kMaxSlots + 1];
    double    m_maxGapByCount[kMaxSlots + 1] = {};

    int64_t  m_touchingQpc = 0;
    int64_t  m_touchingSince = 0;
    int64_t  m_lastDownQpc = 0;

    uint64_t m_totalSamples = 0, m_totalFrames = 0, m_historySamples = 0;
    uint64_t m_messages = 0, m_hidReports = 0, m_downs = 0, m_ups = 0;
    uint64_t m_lostContacts = 0;

    HANDLE   m_lastSourceDevice = nullptr;
    uint32_t m_lastPointerType = 0;

    // Delivery tracking.
    bool     m_edgeSwipeBlocked = false;
    std::vector<HidTrack>         m_hidTracks;      // live and settling
    std::vector<UndeliveredTouch> m_undelivered;
    size_t   m_undeliveredMarkStart = 0;            // first marker still drawn
    uint64_t m_undeliveredDropped = 0;
    uint64_t m_hidFrames = 0, m_hidTouchReports = 0, m_hidNoTipReports = 0, m_hidEmptyReports = 0;
    uint64_t m_hidContacts = 0, m_hidDelivered = 0, m_hidUndelivered = 0;
    uint64_t m_hidOffWindow = 0, m_hidOffWindowBlocked = 0;
    Stats    m_hidMatchOffset, m_deliveredEdge, m_undeliveredEdge;
    RECT     m_hidDisplay{};
    bool     m_hidHaveDisplay = false;

    bool     m_hmSeen = false;
    int32_t  m_hmMinX = 0, m_hmMinY = 0, m_hmMaxX = 0, m_hmMaxY = 0;

    std::vector<InkPt> m_ink;
    size_t   m_inkHead = 0, m_inkCount = 0;
    uint64_t m_inkTotal = 0;

    std::vector<ExportRec> m_recs;
    size_t   m_recHead = 0, m_recCount = 0;
    uint64_t m_recDropped = 0;

    std::vector<POINTER_TOUCH_INFO> m_scratch;

    FILE*        m_log = nullptr;
    std::wstring m_logPath;
    uint64_t     m_logRows = 0;
};

const char* SampleKindName(uint8_t k);

// Column header of a touch pad sample CSV, shared by the export and the live log.
extern const char* const kPadCsvHeader;
