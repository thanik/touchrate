// TouchRate - Windows native touch analyzer
//   Direct3D 11 flip-model presentation, WM_POINTER input with coalesced-frame
//   recovery, Raw Input HID cross-check, and measurement export.
#include "app.h"
#include <shellapi.h>
#include <timeapi.h>
#include <memory>

static App* g_app = nullptr;
static std::vector<uint8_t> g_rawBuf;

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
        if (d.hidMaxContacts) Out("    max contacts : %u (HID descriptor)\n", d.hidMaxContacts);
        if (d.inputReportBytes) Out("    input report : %u bytes\n", d.inputReportBytes);
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

static int RegisterDigitizerRawInput(HWND hwnd)
{
    // Reading the digitizer through Raw Input gives a report count that never
    // passes through the pointer stack, so it can confirm the rate measured
    // from pointer frames.
    static const USHORT usages[] = { 0x04, 0x05, 0x01, 0x02 };
    int ok = 0;
    for (USHORT u : usages)
    {
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x0D;
        rid.usUsage = u;
        rid.dwFlags = RIDEV_INPUTSINK;
        rid.hwndTarget = hwnd;
        if (RegisterRawInputDevices(&rid, 1, sizeof rid)) ++ok;
    }
    return ok;
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
        SetWindowLongW(app.hwnd, GWL_STYLE, (LONG)((app.savedStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP));
        SetWindowPos(app.hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        app.view.fullscreen = true;
    }
    else
    {
        SetWindowLongW(app.hwnd, GWL_STYLE, (LONG)app.savedStyle);
        SetWindowPlacement(app.hwnd, &app.savedPlacement);
        SetWindowPos(app.hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
        app.view.fullscreen = false;
    }
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
    if (notify)
    {
        char b[128];
        snprintf(b, sizeof b, "Re-enumerated: %zu digitizer%s found",
                 app.devices.size(), app.devices.size() == 1 ? "" : "s");
        app.Notify(b, app.devices.empty() ? Pal::warn : Pal::good);
    }
}

// -------------------------------------------------------------------- actions

static void DoExport(App& app)
{
    ExportContext ctx;
    ctx.tracker = &app.tracker;
    ctx.devices = &app.devices;
    ctx.activeDevice = app.activeDevice;
    ctx.monitor = &app.monitor;
    ctx.vblank = &app.vblank;
    ctx.frame = &app.frame;
    ctx.present = app.Present();
    ctx.sessionStartQpc = app.startQpc;
    ctx.nowQpc = QpcNow();

    ExportResult res = ExportAll(ctx, app.exportDir);
    if (res.ok)
    {
        app.lastExportDir = res.dir;
        char b[512];
        double ratio = res.sampleBytesGz ? (double)res.sampleBytesRaw / (double)res.sampleBytesGz : 0.0;
        snprintf(b, sizeof b, "Exported to %s\\  (%llu samples, %.1f MB -> %.1f MB gzip, %.1fx)",
                 WideToUtf8(res.stem).c_str(), (unsigned long long)res.sampleRows,
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
        char b[512];
        snprintf(b, sizeof b, "Live log stopped: %llu rows in %s",
                 (unsigned long long)rows, p.c_str());
        app.Notify(b, Pal::good);
        return;
    }
    // Live logs are raw capture, so they live beside the per-run folders rather
    // than inside one - a run folder is only written when [S] is pressed.
    std::wstring dir = (app.exportDir.empty() ? DefaultExportDir() : app.exportDir) + L"\\live";
    if (!EnsureDirectory(dir)) { app.Notify("Could not create export directory", Pal::bad); return; }
    std::wstring path = dir + L"\\touchrate_" + TimeStampString() + L"_live.csv";
    if (app.tracker.StartLiveLog(path))
    {
        app.lastExportDir = dir;
        app.Notify("Live log started: " + WideToUtf8(path), Pal::good);
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

static void OnKey(App& app, WPARAM key)
{
    switch (key)
    {
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
        app.frame.Reset();
        app.Notify("Measurements reset", Pal::good);
        break;
    case 'C':
        app.tracker.ClearInk();
        app.rend.ClearInk();
        app.inkDrawn = app.tracker.InkTotal();
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
        // Windows from promoting touch to legacy mouse messages and gestures.
        if (app) app->tracker.HandlePointerMessage(msg, wp, QpcNow());
        return 0;

    case WM_POINTERENTER:
    case WM_POINTERLEAVE:
    case WM_POINTERCAPTURECHANGED:
    case WM_TOUCHHITTESTING:
        return 0;

    case WM_POINTERACTIVATE:
        return PA_ACTIVATE;

    case WM_INPUT:
    {
        if (app)
        {
            UINT size = 0;
            if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, nullptr, &size,
                                sizeof(RAWINPUTHEADER)) == 0 && size)
            {
                if (g_rawBuf.size() < size) g_rawBuf.resize(size + 64);
                if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, g_rawBuf.data(), &size,
                                    sizeof(RAWINPUTHEADER)) == size)
                {
                    const RAWINPUT* ri = (const RAWINPUT*)g_rawBuf.data();
                    if (ri->header.dwType == RIM_TYPEHID)
                        app->tracker.HandleRawHidReports(ri->data.hid.dwCount, QpcNow());
                }
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    case WM_POINTERDEVICECHANGE:
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
            const RECT* r = (const RECT*)lp;
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
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = 900;
        mm->ptMinTrackSize.y = 600;
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
    app.frame.Init();
    app.startQpc = QpcNow();
    app.nowQpc = app.startQpc;

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
    DisableTouchFeedback(app.hwnd);
    RegisterDigitizerRawInput(app.hwnd);

    UINT dpi = GetDpiForWindow(app.hwnd);
    if (!app.rend.Init(app.hwnd, dpi))
    {
        MessageBoxW(app.hwnd, L"Could not initialise Direct3D 11.\n"
                              L"A GPU with D3D 10.1 or newer support is required.",
                    L"TouchRate", MB_ICONERROR | MB_OK);
        return 1;
    }

    ShowWindow(app.hwnd, SW_SHOW);
    UpdateWindow(app.hwnd);
    SetForegroundWindow(app.hwnd);

    RefreshDevices(app, false);
    RefreshMonitor(app, true);
    if (opt.fullscreen) ToggleFullscreen(app);
    if (opt.log) ToggleLiveLog(app);

    if (app.devices.empty())
        app.Notify("No touch digitizer detected - press [D] to re-enumerate", Pal::warn);
    else
        app.Notify("Ready. Touch the screen to measure; press [F1] for help.", Pal::good);

    int64_t lastSlowPoll = 0;
    HMONITOR lastMonitor = MonitorFromWindow(app.hwnd, MONITOR_DEFAULTTONEAREST);
    bool running = true;

    while (running)
    {
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
        }

        POINT origin{ 0, 0 };
        ClientToScreen(app.hwnd, &origin);
        app.tracker.SetClientOrigin(origin);
        app.tracker.Update(app.nowQpc);

        app.rend.WaitForPresentSlot();
        app.frame.Tick(QpcNow());

        app.rend.BeginFrame(Pal::bg);
        DrawUi(app);
        app.rend.EndFrame();
        app.rend.Present(app.view.vsync);

        if (app.rend.Occluded())
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
    }

    app.tracker.StopLiveLog();
    app.vblank.Stop();
    app.rend.Shutdown();
    timeEndPeriod(1);
    g_app = nullptr;
    return 0;
}
