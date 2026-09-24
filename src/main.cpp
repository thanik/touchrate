// TouchRate - Windows native touch analyzer
//   Direct3D 11 flip-model presentation, WM_POINTER input with coalesced-frame
//   recovery, Raw Input HID cross-check, and measurement export.
#include "app.h"
#include <shellapi.h>
#include <propsys.h>
#include <timeapi.h>
#include <memory>

static App* g_app = nullptr;
static std::vector<RawReport> g_reports;
static std::vector<uint8_t> g_reportBytes;
static std::vector<HidContactSample> g_hidContacts;

// ------------------------------------------------------------------- console

// A GUI-subsystem binary has no CRT stdout wiring, so text goes straight to the
// inherited handle. That keeps both `TouchRate --list` in a terminal and
// `TouchRate --list > file.txt` working.
static HANDLE g_out = nullptr;
static bool   g_outInit = false;

static void EnsureOut()
{
    if (g_outInit) return;
    g_outInit = true;

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN)
    {
        g_out = h;
        return;
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && !AllocConsole()) return;

    h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN) { g_out = h; return; }
    h = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) g_out = h;
}

static void Out(const char* fmt, ...)
{
    EnsureOut();
    if (!g_out) return;
    char buf[4096];
    va_list ap; va_start(ap, fmt);
    int n = _vsnprintf_s(buf, sizeof buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n > 0) { DWORD written = 0; WriteFile(g_out, buf, (DWORD)n, &written, nullptr); }
}

static void PrintUsage()
{
    Out("\nTouchRate - touch screen polling rate, latency and 10-finger analyzer\n\n"
        "  TouchRate.exe [options]\n\n"
        "  --list              print detected touch digitizers (VID/PID) and exit\n"
        "  --vsync             start with vsync on (default: immediate present)\n"
        "  --fullscreen        start borderless full screen\n"
        "  --no-history        do not recover coalesced pointer frames\n"
        "  --capacity=N        sample rows buffered for export (default 500000)\n"
        "  --export-dir=PATH   where [S] writes files (default: <exe dir>\\exports)\n"
        "  --log               start streaming every sample to CSV immediately\n"
        "  --help              this text\n\n"
        "In the app: [S] export  [L] live log  [R] reset  [V] vsync  [H] history\n"
        "            [F1] help   [Esc] quit\n\n");
}

static void PrintDeviceList()
{
    std::vector<TouchDevice> devs = EnumerateTouchDevices();
    Out("\nTouchRate - %zu digitizer%s found  (SM_MAXIMUMTOUCHES = %d)\n\n",
        devs.size(), devs.size() == 1 ? "" : "s", SystemMaxTouches());
    for (size_t i = 0; i < devs.size(); ++i)
    {
        const TouchDevice& d = devs[i];
        Out("[%zu] %s\n", i, d.Label().c_str());
        Out("    %s", d.VidPidString().c_str());
        if (d.version) Out("   rev 0x%04X", d.version);
        Out("\n");
        if (!d.manufacturer.empty()) Out("    manufacturer : %s\n", WideToUtf8(d.manufacturer).c_str());
        Out("    type         : %s (usage page 0x%02X, usage 0x%02X)\n",
            WideToUtf8(d.typeName).c_str(), d.usagePage, d.usage);
        if (d.maxContacts)    Out("    max contacts : %u (OS)\n", d.maxContacts);
        if (d.hidMaxContacts) Out("    contact slots: %u per HID report\n", d.hidMaxContacts);
        if (d.inputReportBytes) Out("    input report : %u bytes\n", d.inputReportBytes);
        if (d.pressureLevels) Out("    pressure     : %u levels (HID), 0-1024 as Windows passes it on\n", d.pressureLevels);
        if (d.IsPen()) Out("    tilt / twist : %s / %s\n", d.hasTilt ? "yes" : "no", d.hasTwist ? "yes" : "no");
        if (d.haveRects)
            Out("    geometry     : %ld x %ld units -> %ld x %ld px  (%.2f steps/px)\n",
                d.deviceRect.right - d.deviceRect.left, d.deviceRect.bottom - d.deviceRect.top,
                d.displayRect.right - d.displayRect.left, d.displayRect.bottom - d.displayRect.top,
                d.StepsPerPixelX());
        if (!d.path.empty()) Out("    path         : %s\n", WideToUtf8(d.path).c_str());
        Out("\n");
    }
    if (devs.empty()) Out("    (none - no touch screen, pen or touch pad digitizer present)\n\n");
}

// --------------------------------------------------------------- window setup

static void DisableTouchFeedback(HWND hwnd)
{
    // Contact visualisations are drawn by the shell on top of the window and
    // add their own latency; a measurement tool must not show them.
    BOOL off = FALSE;
    static const FEEDBACK_TYPE types[] = {
        FEEDBACK_TOUCH_CONTACTVISUALIZATION, FEEDBACK_TOUCH_TAP,
        FEEDBACK_TOUCH_DOUBLETAP, FEEDBACK_TOUCH_PRESSANDHOLD,
        FEEDBACK_TOUCH_RIGHTTAP, FEEDBACK_GESTURE_PRESSANDTAP,
        FEEDBACK_PEN_BARRELVISUALIZATION, FEEDBACK_PEN_TAP,
        FEEDBACK_PEN_DOUBLETAP, FEEDBACK_PEN_PRESSANDHOLD, FEEDBACK_PEN_RIGHTTAP,
    };
    for (FEEDBACK_TYPE t : types)
        SetWindowFeedbackSetting(hwnd, t, 0, sizeof off, &off);
}

// Windows reserves the screen edges for swipe gestures and hands a touch that
// starts there to the shell instead of the window under the finger. A window
// can opt out while it is full screen; a measurement tool must, or it reports
// the panel as dead along its edges.
static bool BlockEdgeSwipes(HWND hwnd)
{
    // PKEY_EdgeGesture_DisableTouchWhenFullscreen, defined locally so no
    // translation unit needs INITGUID.
    static const PROPERTYKEY kDisableTouchWhenFullscreen = {
        { 0x32CE38B2, 0x2C9A, 0x41B1, { 0x9B, 0xC5, 0xB3, 0x78, 0x43, 0x94, 0xAA, 0x44 } }, 2
    };
    IPropertyStore* store = nullptr;
    if (FAILED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store))) || !store) return false;

    PROPVARIANT v;
    PropVariantInit(&v);
    v.vt = VT_BOOL;
    v.boolVal = VARIANT_TRUE;
    const HRESULT hr = store->SetValue(kDisableTouchWhenFullscreen, v);
    if (SUCCEEDED(hr)) store->Commit();
    store->Release();
    return SUCCEEDED(hr);
}

static void ToggleFullscreen(App& app)
{
    if (!app.view.fullscreen)
    {
        app.savedPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(app.hwnd, &app.savedPlacement);
        app.savedStyle = (DWORD)GetWindowLongW(app.hwnd, GWL_STYLE);

        MONITORINFO mi{};
        mi.cbSize = sizeof mi;
        GetMonitorInfoW(MonitorFromWindow(app.hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        // Set the flag before moving: a move that changes DPI sends
        // WM_DPICHANGED, whose handler sizes the window by it.
        app.view.fullscreen = true;
        SetWindowLongW(app.hwnd, GWL_STYLE, (LONG)((app.savedStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP));
        SetWindowPos(app.hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    }
    else
    {
        // Cleared first for the same reason: restoring onto a monitor with a
        // different DPI must not be re-fitted as full screen.
        app.view.fullscreen = false;
        SetWindowLongW(app.hwnd, GWL_STYLE, (LONG)app.savedStyle);
        // The saved placement is already in the target monitor's pixels. If
        // the move crosses a DPI boundary, WM_DPICHANGED must not scale it again.
        app.restoringPlacement = true;
        SetWindowPlacement(app.hwnd, &app.savedPlacement);
        app.restoringPlacement = false;
        SetWindowPos(app.hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
}

// Window size for the layout's minimum client area, at the window's DPI,
// limited to the monitor's work area so the window can always fit on it.
static SIZE MinWindowSize(const App& app)
{
    int cw = 0, ch = 0;
    MinClientSize(app.rend, cw, ch);

    RECT rc{ 0, 0, cw, ch };
    const DWORD style = (DWORD)GetWindowLongW(app.hwnd, GWL_STYLE);
    const DWORD exStyle = (DWORD)GetWindowLongW(app.hwnd, GWL_EXSTYLE);
    AdjustWindowRectExForDpi(&rc, style, FALSE, exStyle, GetDpiForWindow(app.hwnd));

    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromWindow(app.hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    SIZE out;
    out.cx = std::min<LONG>(rc.right - rc.left, mi.rcWork.right - mi.rcWork.left);
    out.cy = std::min<LONG>(rc.bottom - rc.top, mi.rcWork.bottom - mi.rcWork.top);
    return out;
}

// Grow a window that is below the minimum, keeping it inside the work area.
static void EnforceMinimumSize(App& app)
{
    if (app.view.fullscreen) return;
    const SIZE mn = MinWindowSize(app);
    RECT wr{};
    GetWindowRect(app.hwnd, &wr);
    const LONG w = std::max<LONG>(wr.right - wr.left, mn.cx);
    const LONG h = std::max<LONG>(wr.bottom - wr.top, mn.cy);
    if (w == wr.right - wr.left && h == wr.bottom - wr.top) return;

    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromWindow(app.hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    const LONG x = std::max(mi.rcWork.left, std::min(wr.left, mi.rcWork.right - w));
    const LONG y = std::max(mi.rcWork.top, std::min(wr.top, mi.rcWork.bottom - h));
    SetWindowPos(app.hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void RefreshMonitor(App& app, bool restartVblank)
{
    QueryMonitorInfo(app.hwnd, app.monitor);
    app.rend.SetDpi(app.monitor.dpi);
    if (restartVblank)
    {
        IDXGIOutput* o = app.rend.AcquireContainingOutput();
        app.vblank.Start(o);
        if (o) o->Release();
    }
}

static void RefreshDevices(App& app, bool notify)
{
    app.devices = EnumerateTouchDevices();
    app.activeDevice = FindDeviceByHandle(app.devices, app.tracker.LastSourceDevice());
    app.padDevice = FindDeviceByHandle(app.devices, app.padIn.Device());
    app.penDevice = FindDeviceByHandle(app.devices, app.pen.LastSourceDevice());
    if (notify)
    {
        char b[128];
        snprintf(b, sizeof b, "Re-enumerated: %zu digitizer%s found",
                 app.devices.size(), app.devices.size() == 1 ? "" : "s");
        app.Notify(b, app.devices.empty() ? Pal::warn : Pal::good);
    }
}

// ------------------------------------------------------------ raw input

// Decode what the raw input thread received and route each report by source:
// a touch screen's go to its delivery check, a touch pad's to its own
// measurement. Pad reports never reach the touch screen's figures. A pen's
// are only counted, and tell the delivery check the pen is in range.
static void ProcessRawInput(App& app)
{
    app.rawPump.Drain(g_reports, g_reportBytes);
    for (const RawReport& r : g_reports)
    {
        g_hidContacts.clear();
        HidReportInfo info;
        if (!app.hid.Decode(r.device, g_reportBytes.data() + r.offset, r.len, g_hidContacts, info))
        {
            if (app.hid.IsPen(r.device))
            {
                app.pen.CountHidReport(r.qpc);
                app.tracker.NotePen(r.qpc);
            }
            continue;
        }
        if (info.pad)
        {
            HidDescriptorInfo desc;
            app.hid.Describe(r.device, desc);
            app.padIn.OnReport(r.device, desc, g_hidContacts, info, r.qpc, r.batched);
        }
        else
        {
            app.tracker.HandleHidReport(g_hidContacts, info, r.qpc);
        }
    }
}

static void SetSource(App& app, Source s)
{
    if (s == app.source) return;
    app.source = s;
    if (!app.gridMode)
        app.Notify(s == Source::Pad ? "Showing the touch pad - touch the screen to switch back"
                 : s == Source::Pen ? "Showing the pen - touch the screen to switch back"
                                    : "Showing the touch screen", Pal::textDim);
}

// The analyzer shows whichever input last went down: a finger on the screen
// or the pad, or the pen's tip.
static void FollowSource(App& app)
{
    const uint64_t sd = app.tracker.Downs(), pd = app.pad.Downs(), nd = app.pen.Downs();
    const bool screenNew = sd > app.screenDownsSeen, padNew = pd > app.padDownsSeen;
    const bool penNew = nd > app.penDownsSeen;
    app.screenDownsSeen = sd;
    app.padDownsSeen = pd;
    app.penDownsSeen = nd;
    if (screenNew) SetSource(app, Source::Screen);
    else if (penNew) SetSource(app, Source::Pen);
    else if (padNew) SetSource(app, Source::Pad);
}

// Start on the touch screen if there is one; otherwise on a pen - a drawing
// tablet or pen display - and failing that on a laptop's touch pad.
static Source DefaultSource(const App& app)
{
    bool screen = false, pad = false, pen = false;
    for (const TouchDevice& d : app.devices)
    {
        if (d.IsPad()) pad = true;
        else if (d.IsPen()) pen = true;
        else if ((d.usage == 0x04 || d.maxContacts > 1) && (d.vid || d.pid)) screen = true;
    }
    if (screen) return Source::Screen;
    if (pen) return Source::Pen;
    return pad ? Source::Pad : Source::Screen;
}

// -------------------------------------------------------------------- actions

static void DoExport(App& app)
{
    ExportContext ctx;
    ctx.tracker = &app.tracker;
    ctx.devices = &app.devices;
    ctx.activeDevice = app.activeDevice;
    ctx.hid = &app.hid;
    {
        RECT rc{};
        GetClientRect(app.hwnd, &rc);
        POINT tl{ rc.left, rc.top }, br{ rc.right, rc.bottom };
        ClientToScreen(app.hwnd, &tl);
        ClientToScreen(app.hwnd, &br);
        ctx.window.client = RECT{ tl.x, tl.y, br.x, br.y };
        ctx.window.fullscreen = app.view.fullscreen;
        ctx.window.edgeSwipeBlocked = app.EdgeSwipeBlocked();
    }
    ctx.monitor = &app.monitor;
    ctx.vblank = &app.vblank;
    ctx.frame = &app.frame;
    ctx.grid = &app.grid;
    ctx.pad = &app.pad;
    ctx.padIn = &app.padIn;
    ctx.padDevice = app.padDevice;
    ctx.pen = &app.pen;
    ctx.penDevice = app.penDevice;
    ctx.present = app.Present();
    ctx.sessionStartQpc = app.startQpc;
    ctx.nowQpc = QpcNow();

    ExportResult res = ExportAll(ctx, app.exportDir);
    if (res.ok)
    {
        app.lastExportDir = res.dir;
        char b[512];
        double ratio = res.sampleBytesGz ? (double)res.sampleBytesRaw / (double)res.sampleBytesGz : 0.0;
        char padNote[128] = "";
        int pn = 0;
        if (res.padSampleRows)
            pn += snprintf(padNote + pn, sizeof padNote - pn, " + %llu touch pad samples",
                           (unsigned long long)res.padSampleRows);
        if (res.penSampleRows)
            snprintf(padNote + pn, sizeof padNote - pn, " + %llu pen samples",
                     (unsigned long long)res.penSampleRows);
        snprintf(b, sizeof b, "Exported to %s\\  (%llu samples%s, %.1f MB -> %.1f MB gzip, %.1fx)",
                 WideToUtf8(res.stem).c_str(), (unsigned long long)res.sampleRows, padNote,
                 res.sampleBytesRaw / 1048576.0, res.sampleBytesGz / 1048576.0, ratio);
        app.Notify(b, Pal::good);
    }
    else
    {
        app.Notify("Export failed: " + res.error, Pal::bad);
    }
}

static void ToggleLiveLog(App& app)
{
    if (app.tracker.LiveLogging())
    {
        uint64_t rows = app.tracker.LiveLogRows();
        std::string p = WideToUtf8(app.tracker.LiveLogPath());
        app.tracker.StopLiveLog();
        // The touch pad and the pen stream to files of their own beside it.
        char beside[128] = "";
        int n = 0;
        if (app.pad.LiveLogging())
        {
            n += snprintf(beside + n, sizeof beside - n, ", %llu touch pad rows",
                          (unsigned long long)app.pad.LiveLogRows());
            app.pad.StopLiveLog();
        }
        if (app.pen.LiveLogging())
        {
            n += snprintf(beside + n, sizeof beside - n, "%s%llu pen rows", n ? " and " : ", ",
                          (unsigned long long)app.pen.LiveLogRows());
            app.pen.StopLiveLog();
        }
        char b[512];
        snprintf(b, sizeof b, "Live log stopped: %llu rows in %s%s%s", (unsigned long long)rows,
                 p.c_str(), beside, n ? " beside it" : "");
        app.Notify(b, Pal::good);
        return;
    }
    // Live logs are raw capture, so they live beside the per-run folders rather
    // than inside one - a run folder is only written when [S] is pressed.
    std::wstring dir = (app.exportDir.empty() ? DefaultExportDir() : app.exportDir) + L"\\live";
    if (!EnsureDirectory(dir)) { app.Notify("Could not create export directory", Pal::bad); return; }
    const std::wstring stamp = TimeStampString();
    std::wstring path = dir + L"\\touchrate_" + stamp + L"_live.csv";
    if (app.tracker.StartLiveLog(path))
    {
        app.lastExportDir = dir;
        // A touch pad gets a file of its own: its samples are in pad units.
        // So does a pen, whose samples carry pressure, tilt and buttons.
        bool pad = app.padIn.Seen();
        for (const TouchDevice& d : app.devices) if (d.IsPad()) pad = true;
        if (pad) app.pad.StartLiveLog(dir + L"\\touchrate_" + stamp + L"_touchpad_live.csv");
        if (app.HasPen()) app.pen.StartLiveLog(dir + L"\\touchrate_" + stamp + L"_pen_live.csv");
        std::string also;
        if (app.pad.LiveLogging()) also += " + _touchpad_live.csv";
        if (app.pen.LiveLogging()) also += " + _pen_live.csv";
        app.Notify("Live log started: " + WideToUtf8(path) +
                   (also.empty() ? "" : "  (" + also.substr(1) + ")"), Pal::good);
    }
    else app.Notify("Could not open the live log file", Pal::bad);
}

static void OpenExportFolder(App& app)
{
    std::wstring dir = app.lastExportDir.empty()
                     ? (app.exportDir.empty() ? DefaultExportDir() : app.exportDir)
                     : app.lastExportDir;
    if (!EnsureDirectory(dir)) { app.Notify("No export folder yet", Pal::warn); return; }
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    app.Notify("Opened " + WideToUtf8(dir), Pal::textDim);
}

// ---------------------------------------------------------------- grid scan

// The monitor the touch screen under test is mapped to. On any other screen no
// touch can reach the grid and every cell would read as dead.
static HMONITOR TouchMonitor(const App& app)
{
    auto usable = [](const TouchDevice& d) {
        return d.isPointerDevice && d.monitor && (d.usage == 0x04 || d.maxContacts > 1);
    };
    if (app.activeDevice >= 0 && (size_t)app.activeDevice < app.devices.size() &&
        usable(app.devices[(size_t)app.activeDevice]))
        return app.devices[(size_t)app.activeDevice].monitor;
    // Prefer real hardware over the virtual digitizer Windows may expose.
    for (const TouchDevice& d : app.devices)
        if (usable(d) && (d.vid || d.pid)) return d.monitor;
    for (const TouchDevice& d : app.devices)
        if (usable(d)) return d.monitor;
    return nullptr;
}

static void SetGridMode(App& app, bool on)
{
    if (on == app.gridMode) return;
    if (on)
    {
        // Windows only stops taking edge touches in full screen; windowed, the
        // outer cells would read as dead on a perfect panel.
        app.gridWasFullscreen = app.view.fullscreen;
        if (!app.view.fullscreen) ToggleFullscreen(app);

        HMONITOR target = TouchMonitor(app);
        if (target && target != MonitorFromWindow(app.hwnd, MONITOR_DEFAULTTONEAREST))
        {
            MONITORINFO mi{};
            mi.cbSize = sizeof mi;
            if (GetMonitorInfoW(target, &mi))
                SetWindowPos(app.hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                             mi.rcMonitor.right - mi.rcMonitor.left,
                             mi.rcMonitor.bottom - mi.rcMonitor.top,
                             SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        }
        RefreshMonitor(app, true);
        app.view.showHelp = false;
        app.gridMode = true;
        app.gridResync = true;
        app.Notify("Grid scan - drag slowly over the whole screen; cells that stay dark are dead zones",
                   Pal::good);
    }
    else
    {
        app.gridMode = false;
        if (!app.gridWasFullscreen && app.view.fullscreen) ToggleFullscreen(app);
        RefreshMonitor(app, true);
        app.Notify("Analyzer", Pal::textDim);
    }
}

// Bin the delivered samples that arrived since the last frame.
static void UpdateGridScan(App& app)
{
    RECT area = app.monitor.rect;
    if (area.right <= area.left || area.bottom <= area.top)
    {
        RECT rc{};
        GetClientRect(app.hwnd, &rc);
        POINT o{ 0, 0 };
        ClientToScreen(app.hwnd, &o);
        area = RECT{ o.x, o.y, o.x + rc.right, o.y + rc.bottom };
    }
    app.grid.SetArea(area);

    const Tracker& t = app.tracker;
    const uint64_t total = t.InkTotal();

    // Samples taken while the window was switching to full screen still carry
    // the old client origin; start from the first settled frame instead.
    if (app.gridResync) { app.gridInk = total; app.gridResync = false; return; }

    uint64_t fresh = total - app.gridInk;
    if (fresh > (uint64_t)t.InkCount()) fresh = (uint64_t)t.InkCount();
    const std::vector<InkPt>& ink = t.Ink();
    const size_t cap = ink.size();
    if (fresh && cap)
    {
        const POINT o = t.ClientOrigin();
        const size_t start = (t.InkHead() + cap - (size_t)fresh) % cap;
        for (size_t i = 0; i < (size_t)fresh; ++i)
        {
            const InkPt& p = ink[(start + i) % cap];
            app.grid.Add((int)std::floor(p.x) + o.x, (int)std::floor(p.y) + o.y);
        }
    }
    app.gridInk = total;
}

// Keys on the grid screen. Returns false to fall through to the shared ones.
static bool OnGridKey(App& app, WPARAM key)
{
    switch (key)
    {
    case VK_ESCAPE:
        if (app.view.showHelp) app.view.showHelp = false;
        else SetGridMode(app, false);
        return true;
    case VK_TAB:
        SetGridMode(app, false);
        return true;
    case 'C': case 'R':
        app.grid.Clear();
        app.Notify("Grid cleared", Pal::textDim);
        return true;
    case VK_OEM_PLUS: case VK_ADD:
        if (app.grid.Finer())
        {
            char b[96];
            snprintf(b, sizeof b, "Finer grid: %d x %d cells", app.grid.Cols(), app.grid.Rows());
            app.Notify(b, Pal::textDim);
        }
        return true;
    case VK_OEM_MINUS: case VK_SUBTRACT:
        if (app.grid.Coarser())
        {
            char b[96];
            snprintf(b, sizeof b, "Coarser grid: %d x %d cells", app.grid.Cols(), app.grid.Rows());
            app.Notify(b, Pal::textDim);
        }
        return true;
    // Shared keys that make sense here.
    case VK_F1: case 'S': case 'L': case 'O': case 'D': case 'V': case 'F': case VK_F11:
        return false;
    default:
        return true;   // analyzer view toggles do nothing on this screen
    }
}

static void OnKey(App& app, WPARAM key)
{
    if (app.gridMode && OnGridKey(app, key)) return;

    switch (key)
    {
    case VK_TAB: SetGridMode(app, true); break;
    case VK_ESCAPE:
        if (app.view.showHelp) app.view.showHelp = false;
        else PostMessageW(app.hwnd, WM_CLOSE, 0, 0);
        break;
    case VK_F1: app.view.showHelp = !app.view.showHelp; break;
    case 'S': DoExport(app); break;
    case 'L': ToggleLiveLog(app); break;
    case 'O': OpenExportFolder(app); break;
    case 'R':
        app.tracker.ResetStats();
        app.pad.ResetStats();
        app.padIn.ResetStats();
        app.pen.ResetStats();
        app.frame.Reset();
        app.Notify("Measurements reset", Pal::good);
        break;
    case 'C':
        app.tracker.ClearInk();
        app.pad.ClearInk();
        app.pen.ClearInk();
        app.rend.ClearInk();
        app.inkDrawn = app.tracker.InkTotal();
        app.penInkDrawn = app.pen.InkTotal();
        app.Notify("Ink cleared", Pal::textDim);
        break;
    case 'V':
        app.view.vsync = !app.view.vsync;
        app.Notify(app.view.vsync ? "Vsync ON - presents wait for the panel"
                                  : "Vsync OFF - immediate present, lowest latency",
                   app.view.vsync ? Pal::warn : Pal::good);
        break;
    case 'H':
        app.tracker.SetUseHistory(!app.tracker.UseHistory());
        app.pen.SetUseHistory(app.tracker.UseHistory());
        app.Notify(app.tracker.UseHistory()
                   ? "Coalesced-frame recovery ON - showing hardware report rate"
                   : "Coalesced-frame recovery OFF - showing delivered message rate",
                   app.tracker.UseHistory() ? Pal::good : Pal::warn);
        break;
    case 'T': app.view.showTrails = !app.view.showTrails; break;
    case 'G': app.view.showGrid = !app.view.showGrid; break;
    case 'I': app.view.showInk = !app.view.showInk; break;
    case 'P': app.view.showDots = !app.view.showDots; break;
    case 'D': RefreshDevices(app, true); break;
    case 'F': case VK_F11: ToggleFullscreen(app); break;
    default: break;
    }
}

// ------------------------------------------------------------------- wndproc

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    App* app = g_app;

    switch (msg)
    {
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
        // Handling these (rather than deferring to DefWindowProc) is what keeps
        // Windows from promoting touch and pen to legacy mouse messages and
        // gestures. A pen goes to a tracker of its own; mouse input promoted
        // into the pointer stack is ignored.
        if (app)
        {
            const int64_t now = QpcNow();
            POINTER_INPUT_TYPE type = PT_POINTER;
            const bool typed = GetPointerType(GET_POINTERID_WPARAM(wp), &type) != FALSE;
            if (typed && type == PT_PEN)
            {
                app->pen.HandlePointerMessage(msg, wp, now);
                app->tracker.NotePen(now);
            }
            else if (!typed || type == PT_TOUCH)
                app->tracker.HandlePointerMessage(msg, wp, now);
        }
        return 0;

    case WM_POINTERLEAVE:
        // A pen that leaves detection range stops hovering.
        if (app) app->pen.PointerLeft(GET_POINTERID_WPARAM(wp));
        return 0;

    case WM_POINTERENTER:
    case WM_POINTERCAPTURECHANGED:
    case WM_TOUCHHITTESTING:
        return 0;

    case WM_POINTERACTIVATE:
        return PA_ACTIVATE;

    case WM_INPUT:
        // Only when the raw input thread could not start: the reports are
        // queued the same way, stamped when this window gets to them.
        if (app) app->rawPump.OnInput(lp);
        return DefWindowProcW(hwnd, msg, wp, lp);

    case WM_POINTERDEVICECHANGE:
        // Handles and descriptors may have changed; decode afresh.
        if (app) { app->hid.Reset(); RefreshDevices(*app, false); }
        return 0;

    case WM_POINTERDEVICEINRANGE:
    case WM_POINTERDEVICEOUTOFRANGE:
        if (app) RefreshDevices(*app, false);
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (app)
        {
            if (msg == WM_SYSKEYDOWN && wp == VK_RETURN) { ToggleFullscreen(*app); return 0; }
            OnKey(*app, wp);
            if (msg == WM_KEYDOWN) return 0;
        }
        break;

    case WM_SIZE:
        if (app && wp != SIZE_MINIMIZED)
            app->rend.Resize(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_DPICHANGED:
        if (app)
        {
            // The suggested rectangle keeps the window's logical size, which is
            // wrong for a full screen window: it must cover its new monitor.
            // Rebuild the fonts at the new DPI first: the resize below asks for
            // the minimum size, which must be measured at the new scale.
            app->rend.SetDpi(HIWORD(wp));

            RECT fit;
            const RECT* r = (const RECT*)lp;
            if (app->view.fullscreen)
            {
                MONITORINFO mi{};
                mi.cbSize = sizeof mi;
                GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
                fit = mi.rcMonitor;
                r = &fit;
            }
            if (!app->restoringPlacement)
                SetWindowPos(hwnd, nullptr, r->left, r->top,
                             r->right - r->left, r->bottom - r->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            RefreshMonitor(*app, false);
        }
        return 0;

    case WM_DISPLAYCHANGE:
        if (app) RefreshMonitor(*app, true);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_GETMINMAXINFO:
    {
        // Full screen is sized to its monitor, which may be smaller than the
        // minimum; the layout copes with that case on its own.
        if (app && app->ready && !app->view.fullscreen)
        {
            const SIZE mn = MinWindowSize(*app);
            MINMAXINFO* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = mn.cx;
            mm->ptMinTrackSize.y = mn.cy;
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------------------ arguments

struct Options
{
    bool list = false, help = false;
    bool vsync = false, fullscreen = false, log = false;
    bool history = true;
    size_t capacity = 500000;
    std::wstring exportDir;
};

static Options ParseArgs()
{
    Options o;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return o;
    for (int i = 1; i < argc; ++i)
    {
        std::wstring a = argv[i];
        if (a == L"--list") o.list = true;
        else if (a == L"--help" || a == L"-h" || a == L"/?") o.help = true;
        else if (a == L"--vsync") o.vsync = true;
        else if (a == L"--fullscreen") o.fullscreen = true;
        else if (a == L"--no-history") o.history = false;
        else if (a == L"--log") o.log = true;
        else if (a.rfind(L"--capacity=", 0) == 0)
        {
            long long v = _wtoll(a.c_str() + 11);
            if (v > 1000) o.capacity = (size_t)v;
        }
        else if (a.rfind(L"--export-dir=", 0) == 0) o.exportDir = a.substr(13);
        else o.help = true;
    }
    LocalFree(argv);
    return o;
}

// ----------------------------------------------------------------------- main

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    // Physical pixels only: reported touch coordinates must not be scaled.
    if (HMODULE u32 = GetModuleHandleW(L"user32.dll"))
    {
        using SetCtx = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        if (auto fn = (SetCtx)GetProcAddress(u32, "SetProcessDpiAwarenessContext"))
            fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    Options opt = ParseArgs();
    if (opt.help) { PrintUsage(); return 0; }
    if (opt.list) { PrintDeviceList(); return 0; }

    // App carries the per-contact trail buffers, far too much for the stack.
    std::unique_ptr<App> appPtr(new App());
    App& app = *appPtr;
    g_app = &app;
    app.view.vsync = opt.vsync;
    app.exportDir = opt.exportDir;
    app.tracker.Init(opt.capacity);
    app.tracker.SetUseHistory(opt.history);
    // A pad reports at most five or so contacts, so half the rows go as far.
    app.pad.Init(opt.capacity / 2, Source::Pad);
    app.padIn.Init(&app.pad);
    // A pen is one contact, however fast it reports.
    app.pen.Init(opt.capacity / 2, Source::Pen);
    app.pen.SetUseHistory(opt.history);
    app.frame.Init();
    app.startQpc = QpcNow();
    app.nowQpc = app.startQpc;

    // The shell property store used to block edge swipes is a COM interface.
    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // A measurement tool should not be the thing that adds the jitter.
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    timeBeginPeriod(1);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"TouchRateWindow";
    if (!RegisterClassExW(&wc)) return 1;

    RECT wa{ 0, 0, 1680, 980 };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int ww = std::min<int>(1680, wa.right - wa.left - 40);
    int wh = std::min<int>(1000, wa.bottom - wa.top - 40);
    int wx = wa.left + ((wa.right - wa.left) - ww) / 2;
    int wy = wa.top + ((wa.bottom - wa.top) - wh) / 2;

    app.hwnd = CreateWindowExW(0, wc.lpszClassName, L"TouchRate",
                               WS_OVERLAPPEDWINDOW, wx, wy, ww, wh,
                               nullptr, nullptr, hInst, nullptr);
    if (!app.hwnd) return 1;

    app.tracker.SetWindow(app.hwnd);
    app.pen.SetWindow(app.hwnd);
    DisableTouchFeedback(app.hwnd);
    // Digitizer reports arrive on a thread of their own, which stamps each
    // one as it lands; failing that, on this window.
    if (!app.rawPump.Start()) app.rawPump.Attach(app.hwnd);
    app.edgeSwipeApplied = BlockEdgeSwipes(app.hwnd);

    UINT dpi = GetDpiForWindow(app.hwnd);
    if (!app.rend.Init(app.hwnd, dpi))
    {
        MessageBoxW(app.hwnd, L"Could not initialise Direct3D 11.\n"
                              L"A GPU with D3D 10.1 or newer support is required.",
                    L"TouchRate", MB_ICONERROR | MB_OK);
        return 1;
    }
    // Layout metrics exist only once the renderer has built its fonts; the
    // window was created before that, so bring it up to the minimum now.
    app.ready = true;
    EnforceMinimumSize(app);

    ShowWindow(app.hwnd, SW_SHOW);
    UpdateWindow(app.hwnd);
    SetForegroundWindow(app.hwnd);

    RefreshDevices(app, false);
    app.source = DefaultSource(app);
    RefreshMonitor(app, true);
    if (opt.fullscreen) ToggleFullscreen(app);
    if (opt.log) ToggleLiveLog(app);

    if (app.devices.empty())
        app.Notify("No touch digitizer detected - press [D] to re-enumerate", Pal::warn);
    else if (app.ShowingPad())
        app.Notify("Ready. Put fingers on the touch pad to measure it; press [F1] for help.", Pal::good);
    else if (app.ShowingPen())
        app.Notify("Ready. Draw with the pen to measure it; press [F1] for help.", Pal::good);
    else
        app.Notify("Ready. Touch the screen to measure; press [F1] for help.", Pal::good);

    int64_t lastSlowPoll = 0;
    HMONITOR lastMonitor = MonitorFromWindow(app.hwnd, MONITOR_DEFAULTTONEAREST);
    bool running = true;

    while (running)
    {
        // Raw reports first, so a report is on record before the pointer
        // messages it produced are matched against it.
        ProcessRawInput(app);

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;

        if (IsIconic(app.hwnd))
        {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
            continue;
        }

        app.nowQpc = QpcNow();

        // Occasional refresh of things that cannot change per frame.
        if (app.nowQpc - lastSlowPoll > QpcFreq() / 2)
        {
            lastSlowPoll = app.nowQpc;
            RefreshDwmTiming(app.monitor);
            HMONITOR m = MonitorFromWindow(app.hwnd, MONITOR_DEFAULTTONEAREST);
            if (m != lastMonitor) { lastMonitor = m; RefreshMonitor(app, true); }
            if (app.activeDevice < 0 && app.tracker.LastSourceDevice())
                app.activeDevice = FindDeviceByHandle(app.devices, app.tracker.LastSourceDevice());
            if (app.padIn.Device() &&
                (app.padDevice < 0 || app.devices[(size_t)app.padDevice].rawHandle != app.padIn.Device()))
                app.padDevice = FindDeviceByHandle(app.devices, app.padIn.Device());
            if (app.pen.LastSourceDevice() &&
                (app.penDevice < 0 || app.devices[(size_t)app.penDevice].pointerHandle != app.pen.LastSourceDevice()))
                app.penDevice = FindDeviceByHandle(app.devices, app.pen.LastSourceDevice());
        }

        POINT origin{ 0, 0 };
        ClientToScreen(app.hwnd, &origin);
        app.tracker.SetClientOrigin(origin);
        app.tracker.SetEdgeSwipeBlocked(app.EdgeSwipeBlocked());
        app.tracker.Update(app.nowQpc);
        app.pen.SetClientOrigin(origin);
        app.pen.Update(app.nowQpc);
        app.padIn.Update(app.nowQpc);
        app.pad.Update(app.nowQpc);
        FollowSource(app);
        if (app.gridMode) UpdateGridScan(app);

        app.rend.WaitForPresentSlot();
        app.frame.Tick(QpcNow());

        app.rend.BeginFrame(Pal::bg);
        DrawUi(app);
        app.rend.EndFrame();
        app.rend.Present(app.view.vsync);

        if (app.rend.Occluded())
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
    }

    app.rawPump.Stop();
    app.tracker.StopLiveLog();
    app.pad.StopLiveLog();
    app.pen.StopLiveLog();
    app.vblank.Stop();
    app.rend.Shutdown();
    timeEndPeriod(1);
    if (SUCCEEDED(comInit)) CoUninitialize();
    g_app = nullptr;
    return 0;
}
