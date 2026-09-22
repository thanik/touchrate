#include "touchpad.h"

namespace {

constexpr double kContinuationMs = 50.0;    // give up on a split frame's remainder
constexpr double kMaxScanStepMs  = 1000.0;  // a larger step is a clock glitch, not a gap
// Check the scan clock over this much touching before trusting it outright.
constexpr double kCheckSpanMs    = 2000.0;
constexpr uint64_t kCheckFrames  = 100;
constexpr uint64_t kStepFrames   = 50;      // clock steps seen before judging its resolution

} // namespace

void TouchPadInput::ResetStats()
{
    m_reports = m_palms = m_clicks = 0;
    m_maxDeclared = 0;
    m_reportRate.Reset();
    m_arrival.Reset();
    m_added.Reset();
    m_bunched = m_arrSteps = 0;
    m_bunchWait.Reset();
}

void TouchPadInput::OnReport(HANDLE device, const HidDescriptorInfo& desc,
                             const std::vector<HidContactSample>& contacts,
                             const HidReportInfo& info, int64_t arrivalQpc, bool batched)
{
    if (!m_tracker) return;
    if (device != m_device)
    {
        // A second pad is followed only once the first has let go, so the
        // contact ids of the two never mix within one touch.
        if (m_device && m_tracker->ContactsNow() > 0) return;
        if (m_pending) Flush();
        m_device = device;
        m_desc = desc;
        m_prevTouching = false;
        m_lastArrival = m_lastDev = 0;
        m_lastHaveScan = false;
        m_useScan = true;
        m_checked = m_scanRejected = false;
        m_chkScanMs = m_chkArrMs = m_ratio = 0;
        m_chkN = 0;
        m_steps = m_steps1ms = m_steps500us = 0;
        m_stepMinU = m_stepMaxU = 0;
        m_stepMs = 0;
        m_tracker->SetClockResolutionMs(0);
        m_hybrid = false;
        m_palmIds.clear();
    }

    ++m_reports;
    m_reportRate.Tick(arrivalQpc);
    if (info.haveButton)
    {
        if (info.button && !m_button) ++m_clicks;
        m_button = info.button;
    }
    m_maxDeclared = std::max(m_maxDeclared, info.contactCount);

    if (info.frameStart || !m_pending)
    {
        if (m_pending) Flush();   // its continuation never came
        m_frame.clear();
        m_pending = true;
        m_fArrival = arrivalQpc;
        m_fBatched = batched;
        m_fHaveScan = info.haveScanTime;
        m_fScan = info.scanTime;
    }
    else
    {
        m_hybrid = true;          // the rest of a frame the previous report began
    }
    m_frame.insert(m_frame.end(), contacts.begin(), contacts.end());
    if (info.frameEnd) Flush();
}

void TouchPadInput::Update(int64_t now)
{
    if (m_pending && QpcToMs(now - m_fArrival) > kContinuationMs) Flush();
}

void TouchPadInput::Flush()
{
    m_pending = false;

    // Step since the previous frame, on each clock. Only steps within one touch
    // count: between touches the pad is silent and its clock says nothing.
    const bool continuing = m_prevTouching && m_lastArrival;
    double arrMs = -1, scanMs = -1;
    uint64_t scanUnits = 0;
    if (continuing)
    {
        arrMs = QpcToMs(m_fArrival - m_lastArrival);
        if (m_fHaveScan && m_lastHaveScan && m_desc.scanTimeBits)
        {
            const uint64_t mod = m_desc.scanTimeBits >= 32 ? (1ull << 32) : (1ull << m_desc.scanTimeBits);
            scanUnits = ((uint64_t)m_fScan + mod - (uint64_t)m_lastScan) % mod;
            scanMs = (double)scanUnits * 0.1;
        }
    }
    const bool scanOk = scanMs > 0 && scanMs < kMaxScanStepMs;

    // The field counts 100 us, but many pads only advance it in whole
    // milliseconds. Single intervals are then only good to that step, and the
    // most common one says nothing on its own.
    if (scanOk && m_useScan)
    {
        ++m_steps;
        if (scanUnits % 10 == 0) ++m_steps1ms;
        if (scanUnits % 5 == 0) ++m_steps500us;
        m_stepMinU = m_steps == 1 ? scanUnits : std::min(m_stepMinU, scanUnits);
        m_stepMaxU = std::max(m_stepMaxU, scanUnits);
        if (m_steps >= kStepFrames)
        {
            // A perfectly steady pad repeats one value, which says nothing
            // about the step; a coarse clock shows up as neighbouring steps.
            const uint64_t spread = m_stepMaxU - m_stepMinU;
            const double step = (m_steps1ms * 50 >= m_steps * 49 && spread >= 10) ? 1.0
                              : (m_steps500us * 50 >= m_steps * 49 && spread >= 5) ? 0.5 : 0.1;
            if (step != m_stepMs)
            {
                m_stepMs = step;
                m_tracker->SetClockResolutionMs(step);
            }
        }
    }

    // A report that reaches the app with the one before it, although the pad
    // produced it a whole interval later, was held up and batched on the way.
    if (continuing && scanOk && scanMs >= 3.0)
    {
        ++m_arrSteps;
        if (m_fBatched || (arrMs >= 0 && arrMs < 1.0))
        {
            ++m_bunched;
            if (m_prevArrMs > 0) m_bunchWait.Add(m_prevArrMs);
        }
    }
    m_prevArrMs = continuing && !m_fBatched ? arrMs : 0;

    // Reports that shared one message share an arrival time, so their arrival
    // step says nothing about the pad.
    if (continuing && !m_fBatched && arrMs > 0)
    {
        m_arrival.Add(arrMs);
        if (scanOk)
        {
            if (m_useScan) m_added.Add(arrMs - scanMs);
            if (!m_checked)
            {
                m_chkScanMs += scanMs;
                m_chkArrMs += arrMs;
                ++m_chkN;
                if (m_chkArrMs >= kCheckSpanMs && m_chkN >= kCheckFrames)
                {
                    m_checked = true;
                    m_ratio = m_chkScanMs / m_chkArrMs;
                    if (m_ratio < 0.8 || m_ratio > 1.25)
                    {
                        // Not 100 us units, or not a clock at all. Everything
                        // measured so far used it, so start again on arrival times.
                        m_useScan = false;
                        m_scanRejected = true;
                        m_stepMs = 0;
                        m_tracker->SetClockResolutionMs(0);
                        m_tracker->ResetStats();
                        m_arrival.Reset();
                        m_added.Reset();
                        m_bunched = m_arrSteps = 0;
                        m_bunchWait.Reset();
                    }
                }
            }
        }
    }

    // The pad's clock carried forward from the first frame of the touch,
    // which is anchored to its arrival time.
    int64_t dev;
    if (!continuing || !m_lastDev)          dev = m_fArrival;
    else if (m_useScan && scanOk)           dev = m_lastDev + MsToQpc(scanMs);
    else                                    dev = m_lastDev + (m_fArrival - m_lastArrival);

    // Fingers only. The pad flags a palm or other unintended contact with
    // Confidence clear, and Windows ignores it; so does the measurement, but
    // each one is counted.
    m_contacts.clear();
    std::vector<uint32_t> palmsNow;
    for (const HidContactSample& c : m_frame)
    {
        if (!c.confident)
        {
            if (c.tip)
            {
                palmsNow.push_back(c.id);
                if (std::find(m_palmIds.begin(), m_palmIds.end(), c.id) == m_palmIds.end()) ++m_palms;
            }
            continue;
        }
        bool dup = false;
        for (const PadContact& p : m_contacts) if (p.id == c.id) { dup = true; break; }
        if (dup) continue;
        PadContact p;
        p.id = c.id;
        p.tip = c.tip;
        p.x = (float)c.x;
        p.y = (float)c.y;
        m_contacts.push_back(p);
    }
    m_palmIds.swap(palmsNow);

    m_tracker->HandlePadFrame(m_contacts, dev, m_fArrival, ++m_frameNo);

    m_prevTouching = m_tracker->ContactsNow() > 0;
    m_lastArrival = m_fArrival;
    m_lastDev = dev;
    m_lastHaveScan = m_fHaveScan;
    m_lastScan = m_fScan;
}
