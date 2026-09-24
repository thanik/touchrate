// TouchRate - touch sample ingestion and measurement
#pragma once
#include "common.h"
#include "hidtouch.h"

enum SampleKind : uint8_t { SK_DOWN = 0, SK_UPDATE = 1, SK_UP = 2 };

// The input a tracker measures, and the one the analyzer is showing. A laptop
// can have all three, and each is measured on its own.
enum class Source : uint8_t { Screen, Pad, Pen };

constexpr int  kMaxSlots  = 20;
constexpr int  kTrailCap  = 2048;   // live stroke points kept per contact
constexpr size_t kInkCap  = 65536;  // ink points buffered between frames
constexpr int8_t kNoTilt  = -128;   // a pen tilt the device does not report

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
    uint8_t pressure = 0;   // a pen's pressure in 1/255 steps, which sizes its ink
};

// What a sample carries besides its position, as the device reported it: -1,
// or kNoTilt, where it does not report that value.
struct SampleExtras
{
    float   pressure = -1;             // 0..1
    float   cw = -1, ch = -1;          // contact size, pixels
    float   orient = -1;               // a finger's orientation, degrees
    float   rotation = -1;             // a pen's twist, degrees
    int8_t  tiltX = kNoTilt, tiltY = kNoTilt;   // a pen's tilt, degrees
    uint8_t penFlags = 0;              // PEN_FLAG_BARREL / _INVERTED / _ERASER
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
    float    rotation = -1;
    float    dtMs = 0, latencyMs = 0;
    int8_t   tiltX = kNoTilt, tiltY = kNoTilt;
    uint8_t  penFlags = 0;
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
    float    rotation = -1;
    int8_t   tiltX = kNoTilt, tiltY = kNoTilt;
    uint8_t  penFlags = 0;
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

// A pen in range with its tip up. It reports where it is, but it is not a
// contact and is not measured as one.
struct PenHover
{
    uint32_t pointerId = 0;
    float    x = 0, y = 0;             // client pixels
    int8_t   tiltX = kNoTilt, tiltY = kNoTilt;
    uint8_t  penFlags = 0;
    int64_t  hostQpc = 0;              // its latest report; 0 once it has left
};

// Measures one input source. The touch screen feeds it pointer messages; a
// touch pad, which Windows never delivers as touch, feeds a second instance
// whole frames decoded from its HID reports; a pen feeds a third its own
// pointer messages.
class Tracker
{
public:
    void Init(size_t exportCapacity, Source source = Source::Screen);
    bool IsPad() const { return m_source == Source::Pad; }
    bool IsPen() const { return m_source == Source::Pen; }
    void ResetStats();          // clears measurements, keeps live contacts
    void ClearInk();

    void SetWindow(HWND h) { m_hwnd = h; }
    void SetClientOrigin(POINT p) { m_clientOrigin = p; }
    POINT ClientOrigin() const { return m_clientOrigin; }
    void SetUseHistory(bool v) { m_useHistory = v; }
    bool UseHistory() const { return m_useHistory; }

    // Called from the window procedure. Returns accepted sample count.
    int  HandlePointerMessage(UINT msg, WPARAM wParam, int64_t hostQpc);
    // One pen message's samples, newest first as GetPointerPenInfoHistory
    // returns them. Samples with the tip up only move the hover position.
    int  HandlePenHistory(const POINTER_PEN_INFO* newestFirst, uint32_t n, int64_t hostQpc, bool isUp);
    // The pointer left detection range: a pen stops hovering.
    void PointerLeft(uint32_t pointerId);
    // One of the pen's own HID reports, which is not decoded: hovering or
    // touching, it says the pen is in range and how fast it reports.
    void CountHidReport(int64_t now);
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
    // The pen reported at this time. While a pen is in range Windows holds
    // touch back so that a resting palm does not draw, so a panel contact it
    // did not deliver then is not counted as lost.
    void NotePen(int64_t qpc) { if (qpc > m_penSeenQpc) m_penSeenQpc = qpc; }

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
    // Reports that carried the rest of a scan the previous report began.
    uint64_t HidSplitReports()  const { return m_hidSplitReports; }
    uint32_t HidMaxContactCount() const { return m_hidMaxContactCount; }
    // Panel contacts that started on another window, the desktop, the taskbar
    // or this window's frame. Windows rightly delivered them elsewhere, so they
    // are counted here instead of in HidContacts().
    uint64_t HidOffWindow()     const { return m_hidOffWindow; }
    uint64_t HidOffWindowBlocked() const { return m_hidOffWindowBlocked; }   // with edge swipes blocked
    // Panel contacts Windows did not deliver while a pen was in range, which
    // it does on purpose; counted here instead of as lost.
    uint64_t HidPenHeld()       const { return m_hidPenHeld; }
    // Whether a panel contact falls where a pen in range explains its not
    // being delivered.
    bool PenExcuses(const HidTrack& t) const;
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
    // Mean over every gap, including the long ones missed reports leave.
    double AvgHz()    const { return m_intervalMs.n && m_intervalMs.mean > 0 ? 1000.0 / m_intervalMs.mean : 0.0; }
    // The report rate, which every figure is given as: the average of the
    // device's normal gaps between reports - the most common gap and those
    // around it - as reports per second. A gap a missed report leaves sits far
    // from the rest and is not averaged in.
    double RateHz()   const;
    double RateHzAt(int contacts) const;
    // The most common gap, and the rate it alone would give. That matches the
    // report rate for a device that keeps one gap, and misstates one whose gap
    // varies - a panel whose reports fall on a 1 ms clock alternates between two.
    double ModeMs()   const;
    double ModeHz()   const;

    // Whether the gap between reports varies enough for the most common gap
    // alone to misstate the rate. Judged at each contact count separately,
    // since a panel that slows down as fingers are added spreads its gaps for
    // another reason, and only where there are enough intervals to tell.
    bool   GapsJudged() const;
    bool   GapsVary()   const;

    // Resolution of the clock the intervals are timed on, when it is coarser
    // than the histogram: a touch pad's scan time may count whole milliseconds.
    // Even a steady gap then reads as a mix of the steps around it, so its most
    // common gap says nothing; the report rate, an average, is unaffected.
    void   SetClockResolutionMs(double ms) { m_clockResMs = ms; }
    double ClockResolutionMs() const { return m_clockResMs; }
    bool   CoarseClock() const { return m_clockResMs > m_intervalHist.binW * 1.5; }

    // Report rate broken down by how many contacts the digitizer was tracking.
    // Many panels slow down as fingers are added, which is exactly the case a
    // rhythm game hits during dense passages.
    const Stats&     IntervalMsAt(int contacts) const;
    const Histogram& IntervalHistAt(int contacts) const;
    double ModeMsAt(int contacts) const;
    double ModeHzAt(int contacts) const;
    double MeanHzAt(int contacts) const;
    double MaxGapMsAt(int contacts) const;
    bool   HasDataAt(int contacts) const;
    // Time with at least one contact down, a touch still in progress included.
    double TouchingSec(int64_t now) const
    {
        const int64_t open = m_touchingSince && now > m_touchingSince ? now - m_touchingSince : 0;
        return QpcToSec(m_touchingQpc + open);
    }

    // ---- pen: only a pen tracker fills these
    // Pressure is as Windows passes it on, 0..1024 scaled to 0..1, whatever
    // the pen's own resolution. Every figure counts samples with the tip down;
    // the lift, which reads 0, is left out.
    const Stats& Pressure()       const { return m_pressure; }
    const Stats& PressureAtDown() const { return m_pressureAtDown; }   // each stroke's first sample
    uint32_t PressureLevels()     const { return m_pressureLevels; }   // distinct values of 0..1024 seen
    // Pressure of every tip-down sample, newest last; -1 marks a lift.
    const Ring& PressureRing()    const { return m_pressureRing; }
    const Stats& TiltX() const { return m_tiltX; }   // n is 0 when the pen reports no tilt
    const Stats& TiltY() const { return m_tiltY; }
    bool     RotationSeen() const { return m_rotationSeen; }
    uint8_t  PenFlagsSeen() const { return m_penFlagsSeen; }   // every PEN_FLAG_ seen, ORed
    const PenHover& Hover() const { return m_hover; }
    bool     Hovering(int64_t now) const;
    // Latest report from a pen in range, tip up or down; 0 before any.
    int64_t  LastInRange() const { return std::max(m_hover.hostQpc, m_lastPenQpc); }
    // The rate while hovering, measured the same way as the rate with the tip
    // down, over the gaps between consecutive hover reports.
    double   HoverRateHz() const;
    double   HoverLiveHz(int64_t now) const { return m_hoverRate.Hz(now); }
    const Stats& HoverIntervalMs() const { return m_hoverIntervalMs; }
    uint64_t HoverReports() const { return m_hoverReports; }

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
    int  IngestPenFrames(uint32_t pointerId, int64_t hostQpc, bool isUp);
    void AcceptSample(const POINTER_TOUCH_INFO& ti, int64_t hostQpc, bool fromHistory, bool isUp);
    void AcceptSample(const POINTER_INFO& pi, const SampleExtras& ex, int64_t hostQpc,
                      bool fromHistory, bool isUp);
    void AcceptHover(const POINTER_PEN_INFO& pen, int64_t devQpc, int64_t hostQpc);
    void RecordPen(const SampleExtras& ex, uint8_t kind, bool started);
    void AcceptPadContact(const PadContact& pc, int64_t devQpc, int64_t hostQpc, uint32_t frameId);
    double AdvanceContact(Contact& c, uint8_t kind, int64_t devQpc, float x, float y, bool& started);
    void PushInk(float x, float y, int slot, bool fromHistory, float pressure = 0);
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
    void WritePenLogRow(const ExportRec& r);

    Source m_source = Source::Screen;
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
    uint64_t m_hidSplitReports = 0;
    uint32_t m_hidMaxContactCount = 0;
    uint64_t m_hidOffWindow = 0, m_hidOffWindowBlocked = 0;
    uint64_t m_hidPenHeld = 0;
    int64_t  m_penSeenQpc = 0;
    Stats    m_hidMatchOffset, m_deliveredEdge, m_undeliveredEdge;
    RECT     m_hidDisplay{};
    bool     m_hidHaveDisplay = false;

    // Pen.
    Stats    m_pressure, m_pressureAtDown, m_tiltX, m_tiltY;
    std::vector<uint8_t> m_levelSeen;    // one flag per pressure value, 0..1024
    uint32_t m_pressureLevels = 0;
    Ring     m_pressureRing;
    bool     m_rotationSeen = false;
    uint8_t  m_penFlagsSeen = 0;
    PenHover m_hover;
    int64_t  m_lastPenQpc = 0;           // latest tip-down report
    int64_t  m_lastHoverDevQpc = 0;      // for the next hover interval; 0 across a stroke
    Stats    m_hoverIntervalMs;
    Histogram m_hoverHist;
    RateMeter m_hoverRate;
    uint64_t m_hoverReports = 0;
    std::vector<POINTER_PEN_INFO> m_penScratch;

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

// A pen's sample CSV, shared the same way: the header, and one row formatted
// into buf with times relative to t0. Returns the row's length.
extern const char* const kPenCsvHeader;
int FormatPenCsvRow(char* buf, size_t n, uint64_t seq, const ExportRec& r, int64_t t0);
