// TouchRate - monitor identity, refresh-rate measurement, frame timing
#pragma once
#include "common.h"
#include <dxgi1_6.h>
#include <atomic>
#include <thread>

struct MonitorInfo
{
    std::wstring gdiName;       // \\.\DISPLAY1
    std::wstring friendlyName;  // EDID monitor name
    int    width = 0, height = 0, bpp = 0;
    RECT   rect{};
    UINT   dpi = 96;

    double nominalHz = 0;       // exact rational rate from the display config
    bool   nominalExact = false;
    int    modeHz = 0;          // integer rate from EnumDisplaySettings

    double dwmHz = 0;           // DWM's measured refresh rate
    double dwmPeriodMs = 0;
    bool   dwmValid = false;
};

bool QueryMonitorInfo(HWND hwnd, MonitorInfo& out);
void RefreshDwmTiming(MonitorInfo& mi);

// ------------------------------------------------------- vblank rate measurer
// Blocks on the output's vertical blank on a dedicated thread and reports the
// measured interval. This is the only way to see the panel's true rate when it
// differs from the mode table (VRR, overclocked panels, 143.98 Hz quirks).
class VBlankMeter
{
public:
    ~VBlankMeter() { Stop(); }
    void Start(IDXGIOutput* output);
    void Stop();
    bool   Valid()    const { return m_valid.load(std::memory_order_relaxed); }
    double Hz()       const { return m_hz.load(std::memory_order_relaxed); }
    double PeriodMs() const { return m_periodMs.load(std::memory_order_relaxed); }
    double JitterMs() const { return m_jitterMs.load(std::memory_order_relaxed); }
    uint64_t Count()  const { return m_count.load(std::memory_order_relaxed); }

private:
    void Run();
    std::thread          m_th;
    IDXGIOutput*         m_out = nullptr;
    std::atomic<bool>    m_run{ false };
    std::atomic<bool>    m_valid{ false };
    std::atomic<double>  m_hz{ 0 };
    std::atomic<double>  m_periodMs{ 0 };
    std::atomic<double>  m_jitterMs{ 0 };
    std::atomic<uint64_t> m_count{ 0 };
};

// ------------------------------------------------------------------ frame time

struct FrameStats
{
    RateMeter fps;
    Stats     frameMs;        // whole session
    Histogram hist;           // frame-time distribution
    Ring      ring;           // recent frame times, for the graph
    int64_t   lastQpc = 0;
    uint64_t  frames = 0;
    double    curMs = 0;
    double    windowHz = 0;

    // Present-queue health, from DXGI frame statistics (vsync only).
    uint32_t presentCount = 0;
    uint32_t presentRefreshCount = 0;
    int64_t  dropped = 0;
    bool     statsValid = false;

    void Init();
    void Tick(int64_t now);
    void Reset();
    double AvgMs()  const { return frameMs.n ? frameMs.mean : 0; }
    double P99Ms()  const { return hist.total ? hist.Percentile(0.99) : 0; }
    double Low1Fps()const { double p = P99Ms(); return p > 0 ? 1000.0 / p : 0; }
};
