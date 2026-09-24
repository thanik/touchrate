#include "device.h"

extern "C" {
#include <hidsdi.h>
}
#include <map>

// -------------------------------------------------------------- string helpers

std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

static std::wstring Trim(std::wstring s)
{
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return {};
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

// --------------------------------------------------------------- TouchDevice

double TouchDevice::StepsPerPixelX() const
{
    if (!haveRects) return 0;
    double dw = (double)(deviceRect.right - deviceRect.left);
    double pw = (double)(displayRect.right - displayRect.left);
    return pw > 0 ? dw / pw : 0;
}
double TouchDevice::StepsPerPixelY() const
{
    if (!haveRects) return 0;
    double dh = (double)(deviceRect.bottom - deviceRect.top);
    double ph = (double)(displayRect.bottom - displayRect.top);
    return ph > 0 ? dh / ph : 0;
}

std::string TouchDevice::VidPidString() const
{
    char buf[64];
    if (vid || pid) snprintf(buf, sizeof buf, "VID_%04X  PID_%04X", vid, pid);
    else            snprintf(buf, sizeof buf, "VID/PID unavailable");
    return buf;
}

std::string TouchDevice::Label() const
{
    std::wstring n = product;
    if (n.empty()) n = manufacturer;
    if (n.empty()) n = typeName;
    if (n.empty()) n = L"Unknown digitizer";
    return WideToUtf8(n);
}

// ------------------------------------------------------------ HID usage names

static const wchar_t* DigitizerUsageName(uint16_t page, uint16_t usage)
{
    if (page != 0x0D) return L"HID device";
    switch (usage)
    {
    case 0x01: return L"Digitizer";
    case 0x02: return L"Pen";
    case 0x03: return L"Light pen";
    case 0x04: return L"Touch screen";
    case 0x05: return L"Touch pad";
    case 0x06: return L"Whiteboard";
    case 0x07: return L"Coordinate measuring machine";
    case 0x08: return L"3D digitizer";
    case 0x09: return L"Stereo plotter";
    case 0x0A: return L"Articulated arm";
    case 0x0B: return L"Armature";
    case 0x0C: return L"Multi-point digitizer";
    case 0x0E: return L"Device configuration";
    default:   return L"Digitizer (other)";
    }
}

static bool IsTouchLikeUsage(uint16_t page, uint16_t usage)
{
    if (page != 0x0D) return false;
    switch (usage)
    {
    case 0x01: case 0x02: case 0x04: case 0x05: case 0x0C: return true;
    default: return false;
    }
}

// --------------------------------------------------- parse VID/PID from path

static void ParseVidPidFromPath(const std::wstring& path, uint16_t& vid, uint16_t& pid)
{
    auto grab = [&](const wchar_t* key) -> int {
        size_t p = path.find(key);
        if (p == std::wstring::npos) return -1;
        p += wcslen(key);
        int val = 0, digits = 0;
        while (p < path.size() && digits < 4)
        {
            wchar_t c = path[p];
            int d;
            if (c >= L'0' && c <= L'9') d = c - L'0';
            else if (c >= L'a' && c <= L'f') d = c - L'a' + 10;
            else if (c >= L'A' && c <= L'F') d = c - L'A' + 10;
            else break;
            val = val * 16 + d; ++p; ++digits;
        }
        return digits == 4 ? val : -1;
    };
    // Paths use "VID_" / "vid_" depending on the bus; search both.
    int v = grab(L"VID_"); if (v < 0) v = grab(L"vid_");
    int p = grab(L"PID_"); if (p < 0) p = grab(L"pid_");
    if (v >= 0) vid = (uint16_t)v;
    if (p >= 0) pid = (uint16_t)p;
}

// ------------------------------------------------- query HID strings and caps

static void QueryHidDetails(TouchDevice& d)
{
    // Open with zero access rights: enough for descriptor queries, never blocks
    // the touch stack which holds the device exclusively for I/O.
    HANDLE h = CreateFileW(d.path.c_str(), 0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    HIDD_ATTRIBUTES attr{};
    attr.Size = sizeof attr;
    if (HidD_GetAttributes(h, &attr))
    {
        if (attr.VendorID)  d.vid = attr.VendorID;
        if (attr.ProductID) d.pid = attr.ProductID;
        d.version = attr.VersionNumber;
    }

    // Some devices return the string without a terminator, so the buffer is
    // cleared before each call and one character is kept back for it.
    wchar_t buf[256];
    ZeroMemory(buf, sizeof buf);
    if (HidD_GetProductString(h, buf, sizeof buf - sizeof(wchar_t))) d.product = Trim(buf);
    ZeroMemory(buf, sizeof buf);
    if (HidD_GetManufacturerString(h, buf, sizeof buf - sizeof(wchar_t))) d.manufacturer = Trim(buf);

    PHIDP_PREPARSED_DATA pp = nullptr;
    if (HidD_GetPreparsedData(h, &pp) && pp)
    {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS)
        {
            d.inputReportBytes = caps.InputReportByteLength;

            // Each concurrently reportable contact lives in its own link
            // collection, so counting collections that expose X gives the
            // hardware contact count even when the pointer stack is silent.
            USHORT n = 0;
            HidP_GetValueCaps(HidP_Input, nullptr, &n, pp);
            if (n)
            {
                std::vector<HIDP_VALUE_CAPS> vc(n);
                if (HidP_GetValueCaps(HidP_Input, vc.data(), &n, pp) == HIDP_STATUS_SUCCESS)
                {
                    std::vector<USHORT> cols;
                    for (USHORT i = 0; i < n; ++i)
                    {
                        const HIDP_VALUE_CAPS& c = vc[i];
                        USHORT u = c.IsRange ? c.Range.UsageMin : c.NotRange.Usage;
                        const USHORT uMax = c.IsRange ? c.Range.UsageMax : u;
                        auto has = [&](USHORT usage) { return usage >= u && usage <= uMax; };
                        if (c.UsagePage == 0x01 && u == 0x30) // Generic Desktop / X
                            cols.push_back(c.LinkCollection);
                        if (c.UsagePage != 0x0D) continue;
                        if (has(0x30) && !d.pressureLevels)   // Tip Pressure
                        {
                            // A 16-bit field with a maximum of 65535 can come back
                            // sign-extended; its bit size says what it holds.
                            long long lo = c.LogicalMin, hi = c.LogicalMax;
                            if (hi <= lo && c.BitSize > 0 && c.BitSize < 32) { lo = 0; hi = (1LL << c.BitSize) - 1; }
                            if (hi > lo) d.pressureLevels = (uint32_t)std::min<long long>(hi - lo + 1, 1LL << 24);
                        }
                        if (has(0x3D) || has(0x3E)) d.hasTilt = true;    // X / Y Tilt
                        if (has(0x41)) d.hasTwist = true;                // Twist
                    }
                    std::sort(cols.begin(), cols.end());
                    cols.erase(std::unique(cols.begin(), cols.end()), cols.end());
                    d.hidMaxContacts = (uint32_t)cols.size();
                }
            }
        }
        HidD_FreePreparsedData(pp);
    }
    CloseHandle(h);
}

// ------------------------------------------------------------- raw input pass

static void EnumRawInput(std::vector<TouchDevice>& out,
                         std::map<std::wstring, size_t>& byPath)
{
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || count == 0)
        return;

    std::vector<RAWINPUTDEVICELIST> list(count);
    UINT got = count;
    int n = (int)GetRawInputDeviceList(list.data(), &got, sizeof(RAWINPUTDEVICELIST));
    if (n <= 0) return;
    list.resize((size_t)n);

    for (const RAWINPUTDEVICELIST& e : list)
    {
        if (e.dwType != RIM_TYPEHID) continue;

        RID_DEVICE_INFO info{};
        info.cbSize = sizeof info;
        UINT sz = sizeof info;
        if (GetRawInputDeviceInfoW(e.hDevice, RIDI_DEVICEINFO, &info, &sz) == (UINT)-1)
            continue;
        if (!IsTouchLikeUsage(info.hid.usUsagePage, info.hid.usUsage))
            continue;

        UINT nameLen = 0;
        GetRawInputDeviceInfoW(e.hDevice, RIDI_DEVICENAME, nullptr, &nameLen);
        std::wstring path;
        if (nameLen)
        {
            path.resize(nameLen);
            if (GetRawInputDeviceInfoW(e.hDevice, RIDI_DEVICENAME, path.data(), &nameLen) == (UINT)-1)
                path.clear();
            else
                path.resize(wcsnlen(path.c_str(), path.size()));
        }

        TouchDevice d;
        d.path = path;
        d.rawHandle = e.hDevice;
        d.isRawInputDevice = true;
        d.usagePage = info.hid.usUsagePage;
        d.usage = info.hid.usUsage;
        d.typeName = DigitizerUsageName(d.usagePage, d.usage);
        d.vid = (uint16_t)info.hid.dwVendorId;
        d.pid = (uint16_t)info.hid.dwProductId;
        d.version = (uint16_t)info.hid.dwVersionNumber;

        if (!d.path.empty())
        {
            ParseVidPidFromPath(d.path, d.vid, d.pid);
            QueryHidDetails(d);
        }

        if (!d.path.empty() && byPath.count(d.path))
        {
            // Same interface exposed twice: keep the richer record.
            TouchDevice& ex = out[byPath[d.path]];
            if (!ex.rawHandle) ex.rawHandle = d.rawHandle;
            continue;
        }
        if (!d.path.empty()) byPath[d.path] = out.size();
        out.push_back(std::move(d));
    }
}

// -------------------------------------------------------- pointer device pass

static void EnumPointerDevices(std::vector<TouchDevice>& out,
                               std::map<std::wstring, size_t>& byPath)
{
    UINT32 count = 0;
    if (!GetPointerDevices(&count, nullptr) || count == 0) return;

    std::vector<POINTER_DEVICE_INFO> devs(count);
    if (!GetPointerDevices(&count, devs.data())) return;
    devs.resize(count);

    for (const POINTER_DEVICE_INFO& pd : devs)
    {
        // The pointer-device handle is a Raw Input device handle, so the
        // interface path is the reliable key to merge the two enumerations.
        std::wstring path;
        UINT nameLen = 0;
        if (GetRawInputDeviceInfoW(pd.device, RIDI_DEVICENAME, nullptr, &nameLen) != (UINT)-1 && nameLen)
        {
            path.resize(nameLen);
            if (GetRawInputDeviceInfoW(pd.device, RIDI_DEVICENAME, path.data(), &nameLen) == (UINT)-1)
                path.clear();
            else
                path.resize(wcsnlen(path.c_str(), path.size()));
        }

        TouchDevice* d = nullptr;
        if (!path.empty())
        {
            auto it = byPath.find(path);
            if (it != byPath.end()) d = &out[it->second];
        }
        if (!d)
        {
            TouchDevice nd;
            nd.path = path;
            if (!path.empty())
            {
                ParseVidPidFromPath(path, nd.vid, nd.pid);
                QueryHidDetails(nd);
                byPath[path] = out.size();
            }
            out.push_back(std::move(nd));
            d = &out.back();
        }

        d->isPointerDevice = true;
        d->pointerHandle = pd.device;
        d->pointerDeviceType = pd.pointerDeviceType;
        d->monitor = pd.monitor;
        d->displayOrientation = pd.displayOrientation;
        d->maxContacts = pd.maxActiveContacts;

        std::wstring prod = Trim(std::wstring(pd.productString,
            wcsnlen(pd.productString, POINTER_DEVICE_PRODUCT_STRING_MAX)));
        if (d->product.empty() && !prod.empty()) d->product = prod;

        if (d->typeName.empty())
        {
            switch (pd.pointerDeviceType)
            {
            case POINTER_DEVICE_TYPE_INTEGRATED_PEN: d->typeName = L"Integrated pen"; break;
            case POINTER_DEVICE_TYPE_EXTERNAL_PEN:   d->typeName = L"External pen";   break;
            case POINTER_DEVICE_TYPE_TOUCH:          d->typeName = L"Touch screen";   break;
            case POINTER_DEVICE_TYPE_TOUCH_PAD:      d->typeName = L"Touch pad";      break;
            default:                                 d->typeName = L"Pointer device"; break;
            }
        }

        RECT dr{}, sr{};
        if (GetPointerDeviceRects(pd.device, &dr, &sr))
        {
            d->deviceRect = dr;
            d->displayRect = sr;
            d->haveRects = true;
        }
    }
}

// --------------------------------------------------------------------- public

std::vector<TouchDevice> EnumerateTouchDevices()
{
    std::vector<TouchDevice> out;
    std::map<std::wstring, size_t> byPath;
    EnumRawInput(out, byPath);
    EnumPointerDevices(out, byPath);

    // Touch screens first, then pens/pads; pointer-stack devices before
    // raw-input-only ones so the panel leads with what actually reports.
    std::stable_sort(out.begin(), out.end(), [](const TouchDevice& a, const TouchDevice& b) {
        auto score = [](const TouchDevice& d) {
            int s = 0;
            if (d.usage == 0x04 || d.maxContacts > 1) s += 4;
            if (d.isPointerDevice) s += 2;
            if (d.maxContacts) s += 1;
            return s;
        };
        return score(a) > score(b);
    });
    return out;
}

int FindDeviceByHandle(const std::vector<TouchDevice>& devs, HANDLE h)
{
    if (!h) return -1;
    for (size_t i = 0; i < devs.size(); ++i)
        if (devs[i].pointerHandle == h || devs[i].rawHandle == h) return (int)i;

    // Fall back to resolving the handle to an interface path.
    UINT len = 0;
    if (GetRawInputDeviceInfoW(h, RIDI_DEVICENAME, nullptr, &len) == (UINT)-1 || !len) return -1;
    std::wstring path(len, L'\0');
    if (GetRawInputDeviceInfoW(h, RIDI_DEVICENAME, path.data(), &len) == (UINT)-1) return -1;
    path.resize(wcsnlen(path.c_str(), path.size()));
    for (size_t i = 0; i < devs.size(); ++i)
        if (devs[i].path == path) return (int)i;
    return -1;
}

bool SystemHasTouch()
{
    int v = GetSystemMetrics(SM_DIGITIZER);
    return (v & (NID_INTEGRATED_TOUCH | NID_EXTERNAL_TOUCH)) != 0;
}

int SystemMaxTouches()
{
    return GetSystemMetrics(SM_MAXIMUMTOUCHES);
}
