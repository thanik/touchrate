#include "display.h"
#include <dwmapi.h>
#include <shellscalingapi.h>

// ------------------------------------------------------------- monitor lookup

static bool QueryDisplayConfigFor(const std::wstring& gdiName,
                                  double& outHz, std::wstring& outFriendly)
{
    UINT32 nPath = 0, nMode = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &nPath, &nMode) != ERROR_SUCCESS)
        return false;

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(nPath);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nMode);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &nPath, paths.data(),
                           &nMode, modes.data(), nullptr) != ERROR_SUCCESS)
        return false;
    paths.resize(nPath);
    modes.resize(nMode);

    for (const DISPLAYCONFIG_PATH_INFO& p : paths)
    {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof src;
        src.header.adapterId = p.sourceInfo.adapterId;
        src.header.id = p.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS) continue;
        if (gdiName != src.viewGdiDeviceName) continue;

        // Prefer the signal timing from the target mode; it carries the real
        // vertical sync frequency (e.g. 143.998 Hz) rather than a rounded value.
        bool got = false;
        if (p.targetInfo.modeInfoIdx != DISPLAYCONFIG_PATH_MODE_IDX_INVALID &&
            p.targetInfo.modeInfoIdx < modes.size())
        {
            const DISPLAYCONFIG_MODE_INFO& m = modes[p.targetInfo.modeInfoIdx];
            if (m.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
            {
                const DISPLAYCONFIG_RATIONAL& r = m.targetMode.targetVideoSignalInfo.vSyncFreq;
                if (r.Denominator) { outHz = (double)r.Numerator / (double)r.Denominator; got = true; }
            }
        }
        if (!got && p.targetInfo.refreshRate.Denominator)
        {
            outHz = (double)p.targetInfo.refreshRate.Numerator /
                    (double)p.targetInfo.refreshRate.Denominator;
            got = true;
        }

        DISPLAYCONFIG_TARGET_DEVICE_NAME tgt{};
        tgt.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        tgt.header.size = sizeof tgt;
        tgt.header.adapterId = p.targetInfo.adapterId;
        tgt.header.id = p.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&tgt.header) == ERROR_SUCCESS)
            outFriendly = tgt.monitorFriendlyDeviceName;

        return got;
    }
    return false;
}

bool QueryMonitorInfo(HWND hwnd, MonitorInfo& out)
{
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!mon) return false;

    MONITORINFOEXW mi{};
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(mon, &mi)) return false;

    out.gdiName = mi.szDevice;
    out.rect = mi.rcMonitor;

    DEVMODEW dm{};
    dm.dmSize = sizeof dm;
    if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
    {
        out.width = (int)dm.dmPelsWidth;
        out.height = (int)dm.dmPelsHeight;
        out.bpp = (int)dm.dmBitsPerPel;
        out.modeHz = (int)dm.dmDisplayFrequency;
    }
    if (!out.width)
    {
        out.width = mi.rcMonitor.right - mi.rcMonitor.left;
        out.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }

    double hz = 0;
    std::wstring friendly;
    if (QueryDisplayConfigFor(out.gdiName, hz, friendly))
    {
        out.nominalHz = hz;
        out.nominalExact = true;
    }
    else
    {
        out.nominalHz = (double)out.modeHz;
        out.nominalExact = false;
    }
    if (!friendly.empty()) out.friendlyName = friendly;

    UINT dx = 96, dy = 96;
    if (GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy) == S_OK) out.dpi = dx;
    else out.dpi = 96;

    RefreshDwmTiming(out);
    return true;
}

void RefreshDwmTiming(MonitorInfo& mi)
{
    DWM_TIMING_INFO ti{};
    ti.cbSize = sizeof ti;
    if (DwmGetCompositionTimingInfo(nullptr, &ti) == S_OK)
    {
        if (ti.rateRefresh.uiDenominator)
            mi.dwmHz = (double)ti.rateRefresh.uiNumerator / (double)ti.rateRefresh.uiDenominator;
        if (ti.qpcRefreshPeriod)
            mi.dwmPeriodMs = QpcToMs((int64_t)ti.qpcRefreshPeriod);
        mi.dwmValid = mi.dwmHz > 0 || mi.dwmPeriodMs > 0;
        // The composition period is the authoritative measured value; derive Hz
        // from it when the reported ratio disagrees.
        if (mi.dwmPeriodMs > 0) mi.dwmHz = 1000.0 / mi.dwmPeriodMs;
    }
    else
    {
        mi.dwmValid = false;
    }
}

// ------------------------------------------------------------------ VBlankMeter

void VBlankMeter::Start(IDXGIOutput* output)
{
    Stop();
    if (!output) return;
    m_out = output;
    m_out->AddRef();
    m_run.store(true);
    m_th = std::thread([this] { Run(); });
}

void VBlankMeter::Stop()
{
    m_run.store(false);
    if (m_th.joinable()) m_th.join();
    if (m_out) { m_out->Release(); m_out = nullptr; }
    m_valid.store(false);
}

void VBlankMeter::Run()
{
    SetThreadDescription(GetCurrentThread(), L"TouchRate.VBlank");

    const int kWindow = 128;
    std::vector<double> iv;
    iv.reserve(kWindow);
    std::vector<double> sorted;
    int64_t prev = 0;

    while (m_run.load(std::memory_order_relaxed))
    {
        if (FAILED(m_out->WaitForVBlank()))
        {
            m_valid.store(false);
            Sleep(50);
            prev = 0;
            continue;
        }
        int64_t now = QpcNow();
        m_count.fetch_add(1, std::memory_order_relaxed);

        if (prev)
        {
            double ms = QpcToMs(now - prev);
            if (ms > 0.05 && ms < 200.0) iv.push_back(ms);
        }
        prev = now;

        if ((int)iv.size() >= kWindow)
        {
            sorted = iv;
            std::sort(sorted.begin(), sorted.end());
            double med = sorted[sorted.size() / 2];

            // Average only the intervals close to the median so a scheduling
            // hiccup or a skipped vblank cannot bias the measured rate.
            double sum = 0, sum2 = 0; int n = 0;
            for (double v : iv)
                if (v > med * 0.6 && v < med * 1.6) { sum += v; sum2 += v * v; ++n; }

            if (n >= 8)
            {
                double mean = sum / n;
                double var = sum2 / n - mean * mean;
                m_periodMs.store(mean, std::memory_order_relaxed);
                m_hz.store(1000.0 / mean, std::memory_order_relaxed);
                m_jitterMs.store(var > 0 ? std::sqrt(var) : 0.0, std::memory_order_relaxed);
                m_valid.store(true, std::memory_order_relaxed);
            }
            iv.clear();
        }
    }
}

// ------------------------------------------------------------------ FrameStats

void FrameStats::Init()
{
    fps.Init();
    hist.Init(0.0, 0.05, 400);   // 0 .. 20 ms at 0.05 ms resolution
    ring.Init(1024);
    lastQpc = 0;
}

void FrameStats::Tick(int64_t now)
{
    fps.Tick(now);
    ++frames;
    if (lastQpc)
    {
        curMs = QpcToMs(now - lastQpc);
        frameMs.Add(curMs);
        hist.Add(curMs);
        ring.Push((float)curMs);
    }
    lastQpc = now;
    windowHz = fps.Hz(now);
}

void FrameStats::Reset()
{
    frameMs.Reset();
    hist.Reset();
    ring.Reset();
    fps.Reset();
    frames = 0;
    curMs = 0;
    dropped = 0;
    lastQpc = 0;
}
