#include "hidtouch.h"

extern "C" {
#include <hidsdi.h>
}

namespace {

constexpr USAGE kPageGeneric   = 0x01;
constexpr USAGE kPageButton    = 0x09;
constexpr USAGE kPageDigitizer = 0x0D;
constexpr USAGE kUsageX        = 0x30;
constexpr USAGE kUsageY        = 0x31;
constexpr USAGE kTipSwitch     = 0x42;
constexpr USAGE kConfidence    = 0x47;
constexpr USAGE kContactId     = 0x51;
constexpr USAGE kContactCount  = 0x54;
constexpr USAGE kScanTime      = 0x56;

// Millimetres spanned by a value's physical range, or 0 when the descriptor
// does not declare it as a length.
double PhysicalMm(const HIDP_VALUE_CAPS& c)
{
    const double span = (double)c.PhysicalMax - (double)c.PhysicalMin;
    if (span <= 0) return 0;
    double mmPerUnit;
    if (c.Units == 0x11)      mmPerUnit = 10.0;   // SI linear, centimetres
    else if (c.Units == 0x13) mmPerUnit = 25.4;   // English linear, inches
    else return 0;
    int e = (int)(c.UnitsExp & 0xF);              // a signed four-bit exponent
    if (e > 7) e -= 16;
    const double mm = span * mmPerUnit * std::pow(10.0, e);
    return (mm >= 5.0 && mm <= 2000.0) ? mm : 0.0;
}

std::wstring DevicePath(HANDLE h)
{
    UINT len = 0;
    if (GetRawInputDeviceInfoW(h, RIDI_DEVICENAME, nullptr, &len) == (UINT)-1 || !len) return {};
    std::wstring s(len, L'\0');
    if (GetRawInputDeviceInfoW(h, RIDI_DEVICENAME, s.data(), &len) == (UINT)-1) return {};
    s.resize(wcsnlen(s.c_str(), s.size()));
    return s;
}

// The raw input handle is normally accepted by GetPointerDeviceRects; if not,
// find the pointer device with the same interface path.
bool DisplayRectFor(HANDLE h, RECT& display)
{
    RECT dev{};
    if (GetPointerDeviceRects(h, &dev, &display)) return true;

    const std::wstring path = DevicePath(h);
    if (path.empty()) return false;

    UINT32 n = 0;
    if (!GetPointerDevices(&n, nullptr) || !n) return false;
    std::vector<POINTER_DEVICE_INFO> devs(n);
    if (!GetPointerDevices(&n, devs.data())) return false;
    for (UINT32 i = 0; i < n; ++i)
        if (DevicePath(devs[i].device) == path)
            return GetPointerDeviceRects(devs[i].device, &dev, &display) != FALSE;
    return false;
}

} // namespace

HidTouchDecoder::Dev* HidTouchDecoder::Get(HANDLE h)
{
    auto it = m_devs.find(h);
    if (it != m_devs.end()) return &it->second;

    Dev& d = m_devs[h];     // cached even when unusable, so it is probed once

    RID_DEVICE_INFO info{};
    info.cbSize = sizeof info;
    UINT sz = sizeof info;
    if (GetRawInputDeviceInfoW(h, RIDI_DEVICEINFO, &info, &sz) == (UINT)-1) return &d;
    if (info.dwType != RIM_TYPEHID || info.hid.usUsagePage != kPageDigitizer) return &d;
    if (info.hid.usUsage != 0x04 && info.hid.usUsage != 0x05) return &d;   // touch screen / pad
    d.desc.pad = info.hid.usUsage == 0x05;

    UINT ppSize = 0;
    GetRawInputDeviceInfoW(h, RIDI_PREPARSEDDATA, nullptr, &ppSize);
    if (!ppSize) return &d;
    d.pp.resize(ppSize);
    if (GetRawInputDeviceInfoW(h, RIDI_PREPARSEDDATA, d.pp.data(), &ppSize) == (UINT)-1) return &d;
    const auto pp = (PHIDP_PREPARSED_DATA)d.pp.data();

    HIDP_CAPS caps{};
    if (HidP_GetCaps(pp, &caps) != HIDP_STATUS_SUCCESS) return &d;

    USHORT nv = caps.NumberInputValueCaps;
    std::vector<HIDP_VALUE_CAPS> vc(nv ? nv : 1);
    if (nv && HidP_GetValueCaps(HidP_Input, vc.data(), &nv, pp) != HIDP_STATUS_SUCCESS) nv = 0;

    bool haveRange = false;
    for (USHORT i = 0; i < nv; ++i)
    {
        const HIDP_VALUE_CAPS& c = vc[i];
        const USAGE u = c.IsRange ? c.Range.UsageMin : c.NotRange.Usage;
        if (c.UsagePage == kPageGeneric && u == kUsageX)
        {
            d.fingers.push_back(c.LinkCollection);
            if (!haveRange)
            {
                d.desc.xMin = c.LogicalMin;
                d.desc.xMax = c.LogicalMax;
                d.desc.widthMm = PhysicalMm(c);
                d.desc.reportId = c.ReportID;
                haveRange = true;
            }
        }
        else if (c.UsagePage == kPageGeneric && u == kUsageY && d.desc.yMax == 0)
        {
            d.desc.yMin = c.LogicalMin;
            d.desc.yMax = c.LogicalMax;
            d.desc.heightMm = PhysicalMm(c);
        }
        else if (c.UsagePage == kPageDigitizer && u == kScanTime)
        {
            d.desc.hasScanTime = true;
            d.desc.scanTimeBits = std::min<uint32_t>(c.BitSize, 32);
            d.scanLc = c.LinkCollection;
        }
        else if (c.UsagePage == kPageDigitizer && u == kContactCount)
        {
            d.desc.hasContactCount = true;
            d.countLc = c.LinkCollection;
        }
        else if (c.UsagePage == kPageDigitizer && u == kContactId)
        {
            d.desc.hasContactId = true;
        }
    }

    USHORT nb = caps.NumberInputButtonCaps;
    std::vector<HIDP_BUTTON_CAPS> bc(nb ? nb : 1);
    if (nb && HidP_GetButtonCaps(HidP_Input, bc.data(), &nb, pp) != HIDP_STATUS_SUCCESS) nb = 0;
    for (USHORT i = 0; i < nb; ++i)
    {
        const HIDP_BUTTON_CAPS& c = bc[i];
        const USAGE lo = c.IsRange ? c.Range.UsageMin : c.NotRange.Usage;
        const USAGE hi = c.IsRange ? c.Range.UsageMax : c.NotRange.Usage;
        if (c.UsagePage == kPageButton && !d.desc.hasButton)
        {
            // A touch pad's click button: the whole pad, or its left half.
            d.desc.hasButton = true;
            d.buttonLc = c.LinkCollection;
        }
        if (c.UsagePage != kPageDigitizer) continue;
        if (kTipSwitch >= lo && kTipSwitch <= hi) d.desc.hasTipSwitch = true;
        if (kConfidence >= lo && kConfidence <= hi) d.desc.hasConfidence = true;
    }

    std::sort(d.fingers.begin(), d.fingers.end());
    d.fingers.erase(std::unique(d.fingers.begin(), d.fingers.end()), d.fingers.end());
    d.desc.fingerCollections = (uint32_t)d.fingers.size();

    if (d.fingers.empty() || !d.desc.hasTipSwitch || d.desc.xMax <= d.desc.xMin || d.desc.yMax <= d.desc.yMin)
        return &d;

    // Map through the HID logical range, not the pointer device rectangle:
    // the latter is in physical units and can differ from the logical range
    // by several times on each axis.
    d.haveDisplay = DisplayRectFor(h, d.display);
    d.desc.valid = true;
    d.ok = true;
    return &d;
}

bool HidTouchDecoder::Describe(HANDLE device, HidDescriptorInfo& out)
{
    Dev* d = Get(device);
    if (!d || !d->desc.valid) return false;
    out = d->desc;
    return true;
}

bool HidTouchDecoder::Decode(HANDLE device, const BYTE* report, ULONG len,
                             std::vector<HidContactSample>& contacts, HidReportInfo& info)
{
    Dev* d = Get(device);
    if (!d || !d->ok || !len) return false;
    if (d->desc.reportId && report[0] != d->desc.reportId) return false;

    const auto pp = (PHIDP_PREPARSED_DATA)d->pp.data();
    const auto rep = (PCHAR)report;

    info = HidReportInfo{};
    info.pad = d->desc.pad;
    info.display = d->display;
    info.haveDisplay = d->haveDisplay;

    if (d->desc.hasScanTime)
    {
        ULONG v = 0;
        if (HidP_GetUsageValue(HidP_Input, kPageDigitizer, d->scanLc, kScanTime,
                               &v, pp, rep, len) == HIDP_STATUS_SUCCESS)
        {
            info.haveScanTime = true;
            info.scanTime = (uint32_t)v;
        }
    }
    if (d->desc.hasButton)
    {
        USAGE b[16];
        ULONG nb = _countof(b);
        if (HidP_GetUsages(HidP_Input, kPageButton, d->buttonLc, b, &nb, pp, rep, len) == HIDP_STATUS_SUCCESS)
        {
            info.haveButton = true;
            info.button = nb > 0;
        }
    }

    if (d->desc.hasContactCount)
    {
        ULONG cc = 0;
        if (HidP_GetUsageValue(HidP_Input, kPageDigitizer, d->countLc, kContactCount,
                               &cc, pp, rep, len) != HIDP_STATUS_SUCCESS)
            return false;

        // Hybrid mode splits one frame across reports: the first carries the
        // total count, continuation reports carry zero.
        if (cc > 0)                { d->remaining = cc; info.frameStart = true; }
        else if (d->remaining > 0) { info.frameStart = false; }
        else                       { info.frameStart = true; }
        info.contactCount = cc;
    }

    bool anyContact = false, anyTip = false;
    for (USHORT lc : d->fingers)
    {
        if (d->desc.hasContactCount && d->remaining == 0) break;

        ULONG x = 0, y = 0, id = 0;
        if (HidP_GetUsageValue(HidP_Input, kPageGeneric, lc, kUsageX, &x, pp, rep, len) != HIDP_STATUS_SUCCESS)
            continue;
        HidP_GetUsageValue(HidP_Input, kPageGeneric, lc, kUsageY, &y, pp, rep, len);
        if (d->desc.hasContactId)
            HidP_GetUsageValue(HidP_Input, kPageDigitizer, lc, kContactId, &id, pp, rep, len);

        bool tip = false, confident = !d->desc.hasConfidence;
        USAGE usages[32];
        ULONG n = _countof(usages);
        if (HidP_GetUsages(HidP_Input, kPageDigitizer, lc, usages, &n, pp, rep, len) == HIDP_STATUS_SUCCESS)
            for (ULONG k = 0; k < n; ++k)
            {
                if (usages[k] == kTipSwitch) tip = true;
                if (usages[k] == kConfidence) confident = true;
            }

        if (d->desc.hasContactCount && d->remaining) --d->remaining;

        // Without a contact count, an all-zero slot is simply unused.
        if (!d->desc.hasContactCount && !tip && x == 0 && y == 0 && id == 0) continue;

        HidContactSample c;
        c.id = id;
        c.tip = tip;
        c.confident = confident;
        c.x = (int32_t)x;
        c.y = (int32_t)y;
        // A touch pad is not mapped to the screen; its positions stay in pad units.
        if (d->haveDisplay && !d->desc.pad)
        {
            const float w = (float)(d->display.right - d->display.left);
            const float h = (float)(d->display.bottom - d->display.top);
            c.sx = (float)d->display.left + (float)(c.x - d->desc.xMin) * w / (float)(d->desc.xMax - d->desc.xMin);
            c.sy = (float)d->display.top  + (float)(c.y - d->desc.yMin) * h / (float)(d->desc.yMax - d->desc.yMin);
            c.mapped = true;
        }
        contacts.push_back(c);
        anyContact = true;
        if (tip) anyTip = true;
    }

    info.kind = anyTip ? HRK_TOUCH : (anyContact ? HRK_NOTIP : HRK_EMPTY);
    info.frameEnd = !d->desc.hasContactCount || d->remaining == 0;
    (d->desc.pad ? m_lastPad : m_lastScreen) = device;
    return true;
}
