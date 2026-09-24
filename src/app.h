// TouchRate - shared application state
#pragma once
#include "common.h"
#include "render.h"
#include "tracker.h"
#include "device.h"
#include "display.h"
#include "export.h"
#include "gridscan.h"
#include "touchpad.h"
#include "rawinput.h"

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
    bool        ready = false;      // renderer up; layout metrics are valid
    Renderer    rend;
    Tracker     tracker;
    HidTouchDecoder hid;
    RawInputPump rawPump;
    Tracker     pad;              // the touch pad, measured apart from the screen
    TouchPadInput padIn;
    Tracker     pen;              // a pen, measured apart from both
    Source      source = Source::Screen;   // shown: whichever input was touched last
    int         padDevice = -1;   // index in devices of the pad that reported
    int         penDevice = -1;   // and of the pen
    uint64_t    screenDownsSeen = 0, padDownsSeen = 0, penDownsSeen = 0;
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
    uint64_t    penInkDrawn = 0;  // and the pen's
    Rect2       freeArea{};       // canvas region no panel covers, for hint text
    std::wstring exportDir;
    std::wstring lastExportDir;

    // Saved placement for the borderless-fullscreen toggle.
    WINDOWPLACEMENT savedPlacement{};
    DWORD           savedStyle = 0;
    bool            restoringPlacement = false;   // saved size is already per-monitor

    // Grid scan screen (dead zone test).
    GridScan    grid;
    bool        gridMode = false;
    bool        gridWasFullscreen = false;   // restore this on leaving the mode
    bool        gridResync = false;          // skip samples from before entry
    uint64_t    gridInk = 0;                 // tracker ink points already binned

    bool ShowingPad() const { return source == Source::Pad; }
    bool ShowingPen() const { return source == Source::Pen; }
    Tracker& Shown() { return ShowingPad() ? pad : ShowingPen() ? pen : tracker; }
    const Tracker& Shown() const { return ShowingPad() ? pad : ShowingPen() ? pen : tracker; }

    // The device the view is about: the touch pad or pen, or the touch screen
    // that reported last, falling back to the first of each kind enumerated.
    const TouchDevice* ShownDevice() const
    {
        if (ShowingPad())
        {
            if (padDevice >= 0 && (size_t)padDevice < devices.size()) return &devices[(size_t)padDevice];
            for (const TouchDevice& d : devices) if (d.IsPad()) return &d;
            return nullptr;
        }
        if (ShowingPen())
        {
            if (penDevice >= 0 && (size_t)penDevice < devices.size()) return &devices[(size_t)penDevice];
            for (const TouchDevice& d : devices) if (d.IsPen()) return &d;
            return nullptr;
        }
        if (activeDevice >= 0 && (size_t)activeDevice < devices.size()) return &devices[(size_t)activeDevice];
        for (const TouchDevice& d : devices) if (d.IsScreen()) return &d;
        return devices.empty() ? nullptr : &devices[0];
    }
    bool HasPen() const
    {
        for (const TouchDevice& d : devices) if (d.IsPen()) return true;
        return pen.TotalFrames() > 0;
    }

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

// Smallest client size at which the analyzer layout fits, at the renderer's DPI.
void MinClientSize(const Renderer& r, int& w, int& h);
