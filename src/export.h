// TouchRate - measurement export (CSV + human report + JSON summary)
#pragma once
#include "common.h"
#include "tracker.h"
#include "device.h"
#include "display.h"
#include "gridscan.h"
#include "touchpad.h"

struct PresentInfo
{
    std::string adapter;
    bool     tearingSupported = false;
    bool     vsync = false;
    bool     tornPresents = false;
    unsigned bufferCount = 0;
    unsigned maxFrameLatency = 0;
    bool     statsValid = false;
    int64_t  dropped = 0;
};

struct WindowInfo
{
    RECT client{};              // client area in screen coordinates
    bool fullscreen = false;
    bool edgeSwipeBlocked = false;   // in effect: always requested, honoured in full screen
};

struct ExportContext
{
    const Tracker*                  tracker = nullptr;
    const std::vector<TouchDevice>* devices = nullptr;
    int                             activeDevice = -1;
    HidTouchDecoder*                hid = nullptr;
    WindowInfo                      window;
    const MonitorInfo*              monitor = nullptr;
    const VBlankMeter*              vblank = nullptr;
    const FrameStats*               frame = nullptr;
    const GridScan*                 grid = nullptr;   // reported only if a scan was run
    const Tracker*                  pad = nullptr;    // the touch pad, reported only if it was used
    const TouchPadInput*            padIn = nullptr;
    int                             padDevice = -1;
    const Tracker*                  pen = nullptr;    // a pen, reported only if it drew
    int                             penDevice = -1;
    PresentInfo                     present;
    int64_t                         sessionStartQpc = 0;
    int64_t                         nowQpc = 0;
};

struct ExportResult
{
    bool                      ok = false;
    std::wstring              dir;       // the per-run folder
    std::wstring              stem;      // its name, touchrate_<timestamp>
    std::wstring              summary;   // full path to README.md
    std::vector<std::wstring> files;
    std::string               error;
    uint64_t                  sampleRows = 0;
    uint64_t                  sampleBytesRaw = 0;   // sample CSV before compression
    uint64_t                  sampleBytesGz = 0;    // and after
    uint64_t                  padSampleRows = 0;
    uint64_t                  penSampleRows = 0;
};

ExportResult ExportAll(const ExportContext& ctx, const std::wstring& baseDir);

std::wstring DefaultExportDir();
std::wstring TimeStampString();
bool         EnsureDirectory(const std::wstring& path);
std::wstring ExeDirectory();
