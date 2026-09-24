#include "rawinput.h"

namespace {

// A stalled consumer - the render thread inside a window move, say - must not
// grow the queue without bound.
constexpr size_t kMaxQueued = 1 << 16;

// Digitizer usages: touch screen, touch pad, the pen, whose reports are only
// counted, and the generic digitizer collection, which the decoder ignores.
constexpr USHORT kUsages[] = { 0x04, 0x05, 0x01, 0x02 };

} // namespace

bool RawInputPump::Register(HWND target)
{
    int ok = 0;
    for (USHORT u : kUsages)
    {
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x0D;
        rid.usUsage = u;
        rid.dwFlags = RIDEV_INPUTSINK;   // measure whether or not the window has focus
        rid.hwndTarget = target;
        if (RegisterRawInputDevices(&rid, 1, sizeof rid)) ++ok;
    }
    return ok > 0;
}

bool RawInputPump::Start()
{
    if (m_thread) return m_ok;
    m_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_ready) return false;
    m_thread = CreateThread(nullptr, 0, ThreadMain, this, 0, &m_threadId);
    if (!m_thread) { CloseHandle(m_ready); m_ready = nullptr; return false; }
    WaitForSingleObject(m_ready, 5000);
    CloseHandle(m_ready);
    m_ready = nullptr;
    if (!m_ok) Stop();
    return m_ok;
}

void RawInputPump::Stop()
{
    if (!m_thread) return;
    if (m_hwnd) PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    else PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
    WaitForSingleObject(m_thread, 2000);
    CloseHandle(m_thread);
    m_thread = nullptr;
    m_hwnd = nullptr;
    m_ok = false;
}

bool RawInputPump::Attach(HWND hwnd)
{
    return Register(hwnd);
}

DWORD WINAPI RawInputPump::ThreadMain(void* p)
{
    RawInputPump* self = (RawInputPump*)p;
    // Above the render thread, so a report is stamped the moment it arrives.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TouchRateRawInput";
    RegisterClassExW(&wc);

    // Message-only: never shown, never takes focus.
    self->m_hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
                                   HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (self->m_hwnd)
    {
        SetWindowLongPtrW(self->m_hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_ok = self->Register(self->m_hwnd);
    }
    SetEvent(self->m_ready);
    if (!self->m_ok)
    {
        if (self->m_hwnd) DestroyWindow(self->m_hwnd);
        return 0;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        DispatchMessageW(&msg);
    return 0;
}

LRESULT CALLBACK RawInputPump::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    RawInputPump* self = (RawInputPump*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg)
    {
    case WM_INPUT:
        if (self) self->OnInput(lp);
        break;   // DefWindowProc releases the input
    case WM_CLOSE:
        // Registrations belong to this window; drop them before it goes.
        for (USHORT u : kUsages)
        {
            RAWINPUTDEVICE rid{};
            rid.usUsagePage = 0x0D;
            rid.usUsage = u;
            rid.dwFlags = RIDEV_REMOVE;
            RegisterRawInputDevices(&rid, 1, sizeof rid);
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void RawInputPump::OnInput(LPARAM lp)
{
    const int64_t now = QpcNow();

    UINT size = 0;
    if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || !size)
        return;
    if (m_buf.size() < size) m_buf.resize(size + 64);
    if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, m_buf.data(), &size, sizeof(RAWINPUTHEADER)) != size)
        return;

    const RAWINPUT* ri = (const RAWINPUT*)m_buf.data();
    if (ri->header.dwType != RIM_TYPEHID) return;
    const RAWHID& hid = ri->data.hid;
    if (!hid.dwCount || !hid.dwSizeHid) return;

    AcquireSRWLockExclusive(&m_lock);
    for (DWORD i = 0; i < hid.dwCount; ++i)
    {
        if (m_q.size() >= kMaxQueued) { ++m_dropped; continue; }
        RawReport r;
        r.device = ri->header.hDevice;
        r.qpc = now;
        r.offset = (uint32_t)m_qBytes.size();
        r.len = hid.dwSizeHid;
        r.batched = hid.dwCount > 1;
        const BYTE* src = hid.bRawData + (size_t)i * hid.dwSizeHid;
        m_qBytes.insert(m_qBytes.end(), src, src + hid.dwSizeHid);
        m_q.push_back(r);
    }
    ReleaseSRWLockExclusive(&m_lock);
}

void RawInputPump::Drain(std::vector<RawReport>& reports, std::vector<uint8_t>& bytes)
{
    reports.clear();
    bytes.clear();
    AcquireSRWLockExclusive(&m_lock);
    m_q.swap(reports);
    m_qBytes.swap(bytes);
    ReleaseSRWLockExclusive(&m_lock);
}
