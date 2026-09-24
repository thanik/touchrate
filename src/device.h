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
    POINTER_DEVICE_TYPE pointerDeviceType = (POINTER_DEVICE_TYPE)0;   // 0 when not a pointer device

    // A pen's own resolution, from its HID descriptor. Windows scales whatever
    // it declares to 0..1024 before an application sees it.
    uint32_t pressureLevels = 0;    // 0 when the descriptor declares no tip pressure
    bool     hasTilt = false;
    bool     hasTwist = false;

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
    // Contacts the device can track at once. The descriptor's slots are per
    // report, and a panel in hybrid mode splits more contacts than that across
    // several reports, so the device's own declared maximum can be larger.
    uint32_t ContactCapacity() const { return std::max(maxContacts, hidMaxContacts); }
    bool IsPen() const
    {
        return (usagePage == 0x0D && usage == 0x02) ||
               pointerDeviceType == POINTER_DEVICE_TYPE_INTEGRATED_PEN ||
               pointerDeviceType == POINTER_DEVICE_TYPE_EXTERNAL_PEN;
    }
    bool IsPad() const { return usage == 0x05; }
    // A touch screen: anything that is neither a pen nor a touch pad.
    bool IsScreen() const { return !IsPen() && !IsPad(); }
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
