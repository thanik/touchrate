// TouchRate - touch pad measurement
//
// Windows never delivers a touch pad to an application as touch input; it
// turns it into cursor movement and gestures. The pad's own HID reports, read
// through Raw Input, are the only view of it, so they are assembled into
// frames here and fed to a Tracker of their own, kept apart from the touch
// screen's.
//
// Intervals are timed with the pad's scan-time clock, which every precision
// touch pad reports in 100 us units. It is checked against the host clock and
// dropped in favour of arrival times if the two disagree.
#pragma once
#include "tracker.h"

class TouchPadInput
{
public:
    void Init(Tracker* tracker) { m_tracker = tracker; }
    void ResetStats();   // pair with the tracker's ResetStats

    // One decoded report; 'desc' describes the device that sent it.
    void OnReport(HANDLE device, const HidDescriptorInfo& desc,
                  const std::vector<HidContactSample>& contacts,
                  const HidReportInfo& info, int64_t arrivalQpc, bool batched);
    // Completes a frame whose continuation report never came.
    void Update(int64_t now);

    HANDLE Device() const { return m_device; }
    const HidDescriptorInfo& Desc() const { return m_desc; }
    bool   Seen() const { return m_reports > 0; }

    uint64_t Reports() const { return m_reports; }
    double   ReportHz(int64_t now) const { return m_reportRate.Hz(now); }
    bool     Hybrid() const { return m_hybrid; }          // frames split across reports
    uint32_t MaxDeclared() const { return m_maxDeclared; } // largest contact count a frame declared
    uint64_t Palms() const { return m_palms; }            // contacts the pad flagged as not a finger
    uint64_t Clicks() const { return m_clicks; }
    bool     ButtonDown() const { return m_button; }

    // Timing
    bool   ScanClock() const { return m_useScan && m_desc.hasScanTime; }
    bool   ScanRejected() const { return m_scanRejected; }
    bool   ScanChecked() const { return m_checked; }
    double ScanRatio() const { return m_ratio; }          // pad clock / host clock
    const Stats& ArrivalIntervalMs() const { return m_arrival; }
    const Stats& AddedJitterMs() const { return m_added; } // arrival interval minus pad interval
    // Step of the pad's clock once enough frames have been seen, 0 before.
    // Many pads count whole milliseconds in their 100 us field.
    double ClockStepMs() const { return m_stepMs; }

    // Reports that reached the app together with the one before, although the
    // pad produced them a normal interval apart: the way to the app batches them.
    uint64_t Bunched() const { return m_bunched; }
    uint64_t ArrivalSteps() const { return m_arrSteps; }
    const Stats& BunchWaitMs() const { return m_bunchWait; } // the wait before each bunch

private:
    void Flush();

    Tracker* m_tracker = nullptr;
    HANDLE   m_device = nullptr;
    HidDescriptorInfo m_desc;

    // The frame being assembled.
    bool     m_pending = false;
    std::vector<HidContactSample> m_frame;
    int64_t  m_fArrival = 0;
    bool     m_fBatched = false;
    bool     m_fHaveScan = false;
    uint32_t m_fScan = 0;

    // The previous frame.
    bool     m_prevTouching = false;
    int64_t  m_lastArrival = 0, m_lastDev = 0;
    bool     m_lastHaveScan = false;
    uint32_t m_lastScan = 0;
    uint32_t m_frameNo = 0;

    // Scan clock check.
    bool     m_useScan = true;
    bool     m_checked = false, m_scanRejected = false;
    double   m_chkScanMs = 0, m_chkArrMs = 0, m_ratio = 0;
    uint64_t m_chkN = 0;

    // Clock step detection, from the steps of whole 100 us units seen.
    uint64_t m_steps = 0, m_steps1ms = 0, m_steps500us = 0;
    uint64_t m_stepMinU = 0, m_stepMaxU = 0;
    double   m_stepMs = 0;

    uint64_t m_bunched = 0, m_arrSteps = 0;
    double   m_prevArrMs = 0;
    Stats    m_bunchWait;

    uint64_t m_reports = 0, m_palms = 0, m_clicks = 0;
    uint32_t m_maxDeclared = 0;
    bool     m_hybrid = false, m_button = false;
    std::vector<uint32_t> m_palmIds;
    RateMeter m_reportRate;
    Stats    m_arrival, m_added;
    std::vector<PadContact> m_contacts;
};
