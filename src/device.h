// TouchRate - touch digitizer enumeration (VID / PID / capabilities)
#pragma once
#include "common.h"

struct TouchDevice
{
    std::wstring path;          // \\?\HID#VID_xxxx&PID_xxxx#...
    std::wstring product;       // HID product string / pointer-device product string
    std::wstring manufacturer;  // HID manufacturer string
    std::wstring typeName;      // "Touch screen", "Pen", "Touch pad", ...

    uint16_t vid = 0, pid = 0, version = 0;
    uint16_t usagePage = 0, usage = 0;

    uint32_t maxContacts = 0;       // from POINTER_DEVICE_INFO.maxActiveContacts
    uint32_t hidMaxContacts = 0;    // derived from HID link collections
    uint32_t inputReportBytes = 0;  // HID input report length

    RECT deviceRect{};   // digitizer logical extents
    RECT displayRect{};  // mapped display area, pixels
    bool haveRects = false;

    bool     isPointerDevice = false;  // reported by GetPointerDevices
    bool     isRawInputDevice = false; // reported by GetRawInputDeviceList
    HANDLE   rawHandle = nullptr;
    HANDLE   pointerHandle = nullptr;
    HMONITOR monitor = nullptr;
    DWORD    displayOrientation = 0;

    // Derived
    double StepsPerPixelX() const;
    double StepsPerPixelY() const;
    std::string VidPidString() const;   // "VID_04F3 PID_2A00"
    std::string Label() const;          // best display name
};

// Enumerate every touch/pen digitizer, merging Raw Input and Pointer Device data.
std::vector<TouchDevice> EnumerateTouchDevices();

// Resolve a POINTER_INFO::sourceDevice handle to an index in the list (-1 if unknown).
int FindDeviceByHandle(const std::vector<TouchDevice>& devs, HANDLE h);

// True if the system reports a touch-capable digitizer at all.
bool SystemHasTouch();
int  SystemMaxTouches();
