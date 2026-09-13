// TouchRate - raw HID touch report decoding
//
// Decodes the digitizer's own reports, read through Raw Input, into contacts.
// Comparing those against the pointer contacts Windows delivers shows touches
// the panel reported that never reached the application - for example a touch
// claimed by the shell for an edge-swipe gesture.
#pragma once
#include "common.h"
#include <map>

struct HidContactSample
{
    uint32_t id = 0;
    bool     tip = false;          // TipSwitch: the panel reports this contact as touching
    int32_t  x = 0, y = 0;         // HID logical units
    float    sx = 0, sy = 0;       // screen pixels
    bool     mapped = false;       // sx/sy valid
};

enum HidReportKind : uint8_t
{
    HRK_TOUCH = 0,   // at least one contact with TipSwitch set
    HRK_NOTIP = 1,   // contacts present, none touching
    HRK_EMPTY = 2,   // no contacts at all
};

struct HidReportInfo
{
    HidReportKind kind = HRK_EMPTY;
    bool     frameStart = true;    // false for a hybrid-mode continuation report
    uint32_t contactCount = 0;
    RECT     display{};            // screen rectangle the digitizer is mapped to
    bool     haveDisplay = false;
};

// What the descriptor declares; exported so a report can state it.
struct HidDescriptorInfo
{
    bool     valid = false;
    uint32_t fingerCollections = 0;
    bool     hasContactCount = false;
    bool     hasContactId = false;
    bool     hasTipSwitch = false;
    bool     hasConfidence = false;
    int32_t  xMin = 0, xMax = 0, yMin = 0, yMax = 0;
    uint8_t  reportId = 0;
};

class HidTouchDecoder
{
public:
    // False when the device is not a touch screen or touch pad, or the report
    // is not its touch report. Contacts are appended to 'contacts'.
    bool Decode(HANDLE device, const BYTE* report, ULONG len,
                std::vector<HidContactSample>& contacts, HidReportInfo& info);

    bool Describe(HANDLE device, HidDescriptorInfo& out);
    HANDLE LastDevice() const { return m_last; }
    void Reset() { m_devs.clear(); m_last = nullptr; }

private:
    struct Dev
    {
        bool ok = false;
        std::vector<BYTE>   pp;
        std::vector<USHORT> fingers;      // link collections carrying X, in report order
        USHORT   countLc = 0;
        uint32_t remaining = 0;           // contacts still expected in this frame
        HidDescriptorInfo desc;
        RECT     display{};
        bool     haveDisplay = false;
    };
    Dev* Get(HANDLE h);

    std::map<HANDLE, Dev> m_devs;
    HANDLE m_last = nullptr;
};
