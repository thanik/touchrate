// TouchRate - common types and helpers
#pragma once

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------- QPC helpers

inline int64_t QpcFreq()
{
    static int64_t f = [] { LARGE_INTEGER li; QueryPerformanceFrequency(&li); return li.QuadPart; }();
    return f;
}
inline int64_t QpcNow()
{
    LARGE_INTEGER li; QueryPerformanceCounter(&li); return li.QuadPart;
}
inline double QpcToMs(int64_t delta) { return (double)delta * 1000.0 / (double)QpcFreq(); }
inline double QpcToSec(int64_t delta) { return (double)delta / (double)QpcFreq(); }
inline int64_t MsToQpc(double ms) { return (int64_t)(ms * (double)QpcFreq() / 1000.0); }

// ---------------------------------------------------------------------- color

struct Color
{
    float r = 1, g = 1, b = 1, a = 1;
    Color() = default;
    Color(float R, float G, float B, float A = 1.f) : r(R), g(G), b(B), a(A) {}
    Color WithA(float A) const { return Color(r, g, b, A); }
};

inline Color Rgb(uint32_t hex, float a = 1.f)
{
    return Color(((hex >> 16) & 0xFF) / 255.f, ((hex >> 8) & 0xFF) / 255.f, (hex & 0xFF) / 255.f, a);
}

namespace Pal
{
    const Color bg        = Rgb(0x0B0E14);
    const Color panel     = Rgb(0x141922);
    const Color panel2    = Rgb(0x1B2230);
    const Color edge      = Rgb(0x2A3446);
    const Color grid      = Rgb(0x1E2633);
    const Color text      = Rgb(0xD6DEEA);
    const Color textDim   = Rgb(0x7C8798);
    const Color textFaint = Rgb(0x4E5766);
    const Color accent    = Rgb(0x4FC3F7);
    const Color good      = Rgb(0x5BD98A);
    const Color warn      = Rgb(0xFFC24B);
    const Color bad       = Rgb(0xFF5E6B);
    const Color hero      = Rgb(0xFFFFFF);
}

// 20 well-separated contact colors (first 10 are the "ten finger" set)
inline Color SlotColor(int i)
{
    static const uint32_t c[20] = {
        0xFF5E6B, 0xFFA24B, 0xFFE04B, 0x9CE84B, 0x3FD98A,
        0x2FD2C8, 0x4FC3F7, 0x6C8CFF, 0xB07BFF, 0xFF6FD8,
        0xB03038, 0xB06A28, 0xB09A28, 0x6A9A28, 0x229A5E,
        0x1A8C86, 0x2A80A8, 0x4058A8, 0x7048A8, 0xA83C90,
    };
    return Rgb(c[i % 20]);
}

// ------------------------------------------------------------------ utilities

struct Rect2
{
    float x = 0, y = 0, w = 0, h = 0;
    float r() const { return x + w; }
    float b() const { return y + h; }
    Rect2 Inset(float d) const { return Rect2{ x + d, y + d, w - 2 * d, h - 2 * d }; }
    bool Contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double Clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Lerpf(float a, float b, float t) { return a + (b - a) * t; }

std::string  WideToUtf8(const std::wstring& w);
std::wstring Utf8ToWide(const std::string& s);

// ------------------------------------------------------------------ statistics

struct Stats
{
    uint64_t n = 0;
    double   mean = 0, m2 = 0;
    double   mn = 0, mx = 0, last = 0, sum = 0;

    void Add(double v)
    {
        last = v;
        sum += v;
        if (n == 0) { mn = mx = v; }
        else { if (v < mn) mn = v; if (v > mx) mx = v; }
        ++n;
        double d = v - mean;
        mean += d / (double)n;
        m2 += d * (v - mean);
    }
    double Sd() const { return n > 1 ? std::sqrt(m2 / (double)(n - 1)) : 0.0; }
    void Reset() { *this = Stats(); }
};

// Fixed-resolution histogram with percentile estimation.
struct Histogram
{
    double   lo = 0, binW = 1;
    int      nbins = 0;
    uint64_t under = 0, over = 0, total = 0;
    std::vector<uint64_t> bins;

    void Init(double lo_, double binW_, int nb)
    {
        lo = lo_; binW = binW_; nbins = nb;
        bins.assign((size_t)nb, 0);
        under = over = total = 0;
    }
    void Reset()
    {
        std::fill(bins.begin(), bins.end(), (uint64_t)0);
        under = over = total = 0;
    }
    void Add(double v)
    {
        ++total;
        int i = (int)std::floor((v - lo) / binW);
        if (i < 0) { ++under; return; }
        if (i >= nbins) { ++over; return; }
        ++bins[(size_t)i];
    }
    uint64_t MaxBin() const
    {
        uint64_t m = 0; for (uint64_t v : bins) m = (v > m) ? v : m; return m;
    }
    int ModeBin() const
    {
        int best = -1; uint64_t m = 0;
        for (int i = 0; i < nbins; ++i) if (bins[(size_t)i] > m) { m = bins[(size_t)i]; best = i; }
        return best;
    }
    double BinCenter(int i) const { return lo + ((double)i + 0.5) * binW; }
    // Linear-interpolated percentile (p in 0..1). Returns NaN if empty.
    double Percentile(double p) const
    {
        if (total == 0) return std::nan("");
        double target = p * (double)total;
        double acc = (double)under;
        if (acc >= target) return lo;
        for (int i = 0; i < nbins; ++i)
        {
            double c = (double)bins[(size_t)i];
            if (acc + c >= target)
            {
                double frac = c > 0 ? (target - acc) / c : 0.0;
                return lo + ((double)i + frac) * binW;
            }
            acc += c;
        }
        return lo + (double)nbins * binW;
    }
};

// Sliding-window event rate meter (bucketed, allocation free).
struct RateMeter
{
    static const int kBuckets = 64;          // 64 x 15.625 ms = 1.0 s window
    int64_t bucketQpc = 0;
    int64_t stamp[kBuckets] = {};
    double  count[kBuckets] = {};

    void Init()
    {
        bucketQpc = QpcFreq() / kBuckets;    // one second split into kBuckets
        for (int i = 0; i < kBuckets; ++i) { stamp[i] = -1; count[i] = 0; }
    }
    void Tick(int64_t qpc, double c = 1.0)
    {
        if (bucketQpc <= 0) Init();
        int64_t b = qpc / bucketQpc;
        int i = (int)(b % kBuckets);
        if (stamp[i] != b) { stamp[i] = b; count[i] = 0; }
        count[i] += c;
    }
    // Events per second over the trailing 1 s window.
    double Hz(int64_t nowQpc) const
    {
        if (bucketQpc <= 0) return 0;
        int64_t nb = nowQpc / bucketQpc;
        double  s = 0;
        for (int i = 0; i < kBuckets; ++i)
            if (stamp[i] >= 0 && (nb - stamp[i]) < kBuckets) s += count[i];
        return s;
    }
    void Reset() { Init(); }
};

// Ring buffer of floats for scrolling graphs.
struct Ring
{
    std::vector<float> v;
    size_t head = 0, count = 0;
    void Init(size_t n) { v.assign(n, 0.f); head = 0; count = 0; }
    void Push(float f)
    {
        if (v.empty()) return;
        v[head] = f; head = (head + 1) % v.size();
        if (count < v.size()) ++count;
    }
    // i = 0 is oldest of the retained samples.
    float At(size_t i) const
    {
        size_t start = (head + v.size() - count) % v.size();
        return v[(start + i) % v.size()];
    }
    void Reset() { head = 0; count = 0; }
};
