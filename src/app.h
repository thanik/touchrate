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

// The input the analyzer is showing. A laptop can have both, and they are
// measured separately; the view follows whichever was touched last.
enum class Source { Screen, Pad };

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
    Source      source = Source::Screen;
    int         padDevice = -1;   // index in devices of the pad that reported
    uint64_t    screenDownsSeen = 0, padDownsSeen = 0;
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
    bool            restoringPlacement = false;   // saved size is already per-monitor

    // Grid scan screen (dead zone test).
    GridScan    grid;
    bool        gridMode = false;
    bool        gridWasFullscreen = false;   // restore this on leaving the mode
    bool        gridResync = false;          // skip samples from before entry
    uint64_t    gridInk = 0;                 // tracker ink points already binned

    bool ShowingPad() const { return source == Source::Pad; }
    Tracker& Shown() { return ShowingPad() ? pad : tracker; }
    const Tracker& Shown() const { return ShowingPad() ? pad : tracker; }

    // The device the view is about: the touch pad, or the touch screen that
    // reported last, falling back to the first of each kind enumerated.
    const TouchDevice* ShownDevice() const
    {
        if (ShowingPad())
        {
            if (padDevice >= 0 && (size_t)padDevice < devices.size()) return &devices[(size_t)padDevice];
            for (const TouchDevice& d : devices) if (d.usage == 0x05) return &d;
            return nullptr;
        }
        if (activeDevice >= 0 && (size_t)activeDevice < devices.size()) return &devices[(size_t)activeDevice];
        for (const TouchDevice& d : devices) if (d.usage != 0x05) return &d;
        return devices.empty() ? nullptr : &devices[0];
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
