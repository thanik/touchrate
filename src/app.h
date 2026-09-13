// TouchRate - shared application state
#pragma once
#include "common.h"
#include "render.h"
#include "tracker.h"
#include "device.h"
#include "display.h"
#include "export.h"

struct ViewOpts
{
    bool vsync      = false;   // default: immediate present for lowest latency
    bool showGrid   = true;
    bool showInk    = true;
    bool showTrails = true;
    bool showDots   = true;
    bool showHelp   = false;
    bool fullscreen = false;
};

struct Toast
{
    std::string text;
    int64_t     until = 0;
    Color       color = Pal::good;
};

struct App
{
    HWND        hwnd = nullptr;
    Renderer    rend;
    Tracker     tracker;
    HidTouchDecoder hid;
    bool        edgeSwipeApplied = false;   // the shell accepted the window property
    FrameStats  frame;
    MonitorInfo monitor;
    VBlankMeter vblank;
    std::vector<TouchDevice> devices;
    int         activeDevice = -1;
    ViewOpts    view;
    Toast       toast;

    int64_t     startQpc = 0;
    int64_t     nowQpc = 0;
    uint64_t    inkDrawn = 0;     // ink points already committed to the ink layer
    Rect2       freeArea{};       // canvas region no panel covers, for hint text
    std::wstring exportDir;
    std::wstring lastExportDir;

    // Saved placement for the borderless-fullscreen toggle.
    WINDOWPLACEMENT savedPlacement{};
    DWORD           savedStyle = 0;

    // Edge swipes are always blocked, but the shell only honours that while the
    // window is full screen.
    bool EdgeSwipeBlocked() const { return edgeSwipeApplied && view.fullscreen; }

    void Notify(const std::string& s, Color c = Pal::good)
    {
        toast.text = s;
        toast.color = c;
        toast.until = QpcNow() + MsToQpc(4500);
    }
    PresentInfo Present() const
    {
        PresentInfo p;
        p.adapter = rend.AdapterName();
        p.tearingSupported = rend.TearingSupported();
        p.vsync = view.vsync;
        p.tornPresents = rend.LastPresentTorn();
        p.bufferCount = rend.BufferCount();
        p.maxFrameLatency = rend.MaxFrameLatency();
        p.statsValid = rend.PresentStatsValid();
        p.dropped = rend.PresentsDropped();
        return p;
    }
};

void DrawUi(App& app);
