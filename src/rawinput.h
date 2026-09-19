// TouchRate - Raw Input reception on a dedicated thread
//
// HID reports carry no host timestamp. Read on the render thread they would be
// stamped whenever the next frame happened to pump messages - up to a whole
// refresh late with vsync on - so they are received here instead, on a thread
// that does nothing but wait for them, and queued with their arrival time.
#pragma once
#include "common.h"

struct RawReport
{
    HANDLE   device = nullptr;
    int64_t  qpc = 0;          // arrival time
    uint32_t offset = 0;       // into the byte buffer
    uint32_t len = 0;
    bool     batched = false;  // shared one message with other reports, so qpc is shared too
};

class RawInputPump
{
public:
    ~RawInputPump() { Stop(); }

    // Starts the receiving thread and registers the digitizer usages to it.
    // False if either fails; Attach() is the fallback.
    bool Start();
    void Stop();

    // Fallback: receive on an existing window, which forwards WM_INPUT to OnInput.
    bool Attach(HWND hwnd);
    void OnInput(LPARAM lp);

    // Moves every report received since the last call into the caller's buffers.
    void Drain(std::vector<RawReport>& reports, std::vector<uint8_t>& bytes);
    uint64_t Dropped() const { return m_dropped; }
    bool OnThread() const { return m_thread != nullptr; }

private:
    static DWORD WINAPI ThreadMain(void* self);
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    bool Register(HWND target);

    HANDLE  m_thread = nullptr;
    DWORD   m_threadId = 0;
    HWND    m_hwnd = nullptr;
    HANDLE  m_ready = nullptr;
    bool    m_ok = false;

    SRWLOCK m_lock = SRWLOCK_INIT;
    std::vector<RawReport> m_q;
    std::vector<uint8_t>   m_qBytes;
    std::vector<uint8_t>   m_buf;       // receive buffer, used by one thread only
    uint64_t m_dropped = 0;
};
