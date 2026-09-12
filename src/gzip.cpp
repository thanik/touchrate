#include "gzip.h"
#include <cstdarg>
#include <cstring>
#include <ctime>

// ============================================================ RFC 1951 tables

namespace {

// Length codes 257..285: base length and number of extra bits.
const uint16_t kLenBase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
const uint8_t kLenExtra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

// Distance codes 0..29.
const uint16_t kDistBase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
const uint8_t kDistExtra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

// Order in which the code-length alphabet's lengths are stored.
const uint8_t kClOrder[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

constexpr int kNumLitLen = 288;
constexpr int kNumDist   = 30;
constexpr int kNumCl     = 19;

constexpr int kWinSize  = 32768;
constexpr int kMinMatch = 3;
constexpr int kMaxMatch = 258;
constexpr int kHashBits = 15;
constexpr int kHashSize = 1 << kHashBits;
constexpr uint32_t kNil = 0xFFFFFFFFu;
constexpr int kMaxChain = 160;      // search effort per position
constexpr int kNiceMatch = 128;     // stop searching once this good
constexpr size_t kBlockTokens = 16384;

// ------------------------------------------------------------------ bit output

struct BitWriter
{
    std::vector<uint8_t>& out;
    uint32_t buf = 0;
    int      cnt = 0;

    explicit BitWriter(std::vector<uint8_t>& o) : out(o) {}

    // DEFLATE packs bits into bytes starting at the least significant bit.
    void Bits(uint32_t v, int n)
    {
        if (n <= 0) return;
        buf |= (v & ((1u << n) - 1u)) << cnt;
        cnt += n;
        while (cnt >= 8) { out.push_back((uint8_t)(buf & 0xFF)); buf >>= 8; cnt -= 8; }
    }
    void Align()
    {
        if (cnt > 0) { out.push_back((uint8_t)(buf & 0xFF)); buf = 0; cnt = 0; }
    }
};

uint16_t ReverseBits(uint16_t v, int n)
{
    uint16_t r = 0;
    for (int i = 0; i < n; ++i) r = (uint16_t)((r << 1) | ((v >> i) & 1));
    return r;
}

// ------------------------------------------------------------ Huffman building

// Canonical Huffman code lengths, limited to maxLen bits. Frequencies are
// halved and the tree rebuilt if the natural depth exceeds the limit; that
// costs a fraction of a percent of ratio and keeps the code simple.
void BuildLengths(const uint32_t* freqIn, int n, int maxLen, uint8_t* lens)
{
    std::vector<uint32_t> freq(freqIn, freqIn + n);
    memset(lens, 0, (size_t)n);

    for (;;)
    {
        struct Node { uint32_t freq; int left, right; };
        std::vector<Node> nodes;
        nodes.reserve((size_t)n * 2);
        std::vector<int> heap;

        for (int i = 0; i < n; ++i)
            if (freq[(size_t)i])
            {
                nodes.push_back(Node{ freq[(size_t)i], -1 - i, -1 });   // leaf: left encodes symbol
                heap.push_back((int)nodes.size() - 1);
            }

        if (heap.empty()) return;                       // nothing used
        if (heap.size() == 1)
        {
            lens[-1 - nodes[(size_t)heap[0]].left] = 1;
            return;
        }

        auto cmp = [&](int a, int b) { return nodes[(size_t)a].freq > nodes[(size_t)b].freq; };
        std::make_heap(heap.begin(), heap.end(), cmp);

        while (heap.size() > 1)
        {
            std::pop_heap(heap.begin(), heap.end(), cmp);
            int a = heap.back(); heap.pop_back();
            std::pop_heap(heap.begin(), heap.end(), cmp);
            int b = heap.back(); heap.pop_back();

            nodes.push_back(Node{ nodes[(size_t)a].freq + nodes[(size_t)b].freq, a, b });
            heap.push_back((int)nodes.size() - 1);
            std::push_heap(heap.begin(), heap.end(), cmp);
        }

        // Walk the tree to depths, iteratively so a degenerate tree cannot
        // overflow the stack.
        int over = 0;
        std::vector<std::pair<int, int>> stack;   // (node, depth)
        stack.push_back({ heap[0], 0 });
        while (!stack.empty())
        {
            auto [idx, depth] = stack.back();
            stack.pop_back();
            const Node& nd = nodes[(size_t)idx];
            if (nd.right == -1)                      // leaf: left encodes the symbol
            {
                int sym = -1 - nd.left;
                int d = depth < 1 ? 1 : depth;
                if (d > maxLen) { over = 1; break; }
                lens[sym] = (uint8_t)d;
                continue;
            }
            stack.push_back({ nd.left, depth + 1 });
            stack.push_back({ nd.right, depth + 1 });
        }
        if (!over) return;

        memset(lens, 0, (size_t)n);
        for (int i = 0; i < n; ++i)
            if (freq[(size_t)i]) freq[(size_t)i] = (freq[(size_t)i] + 1) / 2;
    }
}

// Canonical codes from lengths (RFC 1951 section 3.2.2), stored bit-reversed
// so the bit writer can emit them directly.
void BuildCodes(const uint8_t* lens, int n, int maxLen, uint16_t* revCodes)
{
    std::vector<uint16_t> blCount((size_t)maxLen + 1, 0);
    for (int i = 0; i < n; ++i) if (lens[i]) ++blCount[lens[i]];

    std::vector<uint16_t> next((size_t)maxLen + 2, 0);
    uint16_t code = 0;
    for (int bits = 1; bits <= maxLen; ++bits)
    {
        code = (uint16_t)((code + blCount[(size_t)bits - 1]) << 1);
        next[(size_t)bits] = code;
    }
    for (int i = 0; i < n; ++i)
    {
        int l = lens[i];
        revCodes[i] = l ? ReverseBits(next[(size_t)l]++, l) : 0;
    }
}

// A tree with a single used symbol is not a complete code; give a second
// symbol a code so every decoder accepts the block.
void EnsureTwoCodes(uint32_t* freq, int n)
{
    int used = 0;
    for (int i = 0; i < n; ++i) if (freq[i]) ++used;
    if (used >= 2) return;
    for (int i = 0; i < n && used < 2; ++i)
        if (!freq[i]) { freq[i] = 1; ++used; }
}

// --------------------------------------------------------------------- tokens

struct Token
{
    uint16_t litlen;   // literal byte, or match length when dist != 0
    uint16_t dist;     // 0 for a literal
};

int LengthCode(int len)
{
    for (int i = 28; i >= 0; --i) if (len >= kLenBase[i]) return i;
    return 0;
}
int DistCode(int dist)
{
    for (int i = 29; i >= 0; --i) if (dist >= kDistBase[i]) return i;
    return 0;
}

struct Deflater
{
    const uint8_t* data = nullptr;
    size_t         size = 0;
    std::vector<uint8_t>& out;
    BitWriter bw;

    std::vector<uint32_t> head, prev;
    std::vector<Token>    tokens;
    uint32_t freqLit[kNumLitLen] = {};
    uint32_t freqDist[kNumDist] = {};

    Deflater(const uint8_t* d, size_t n, std::vector<uint8_t>& o)
        : data(d), size(n), out(o), bw(o)
    {
        head.assign(kHashSize, kNil);
        prev.assign(kWinSize, kNil);
        tokens.reserve(kBlockTokens + 8);
    }

    uint32_t Hash(size_t p) const
    {
        return (uint32_t)(((data[p] << 10) ^ (data[p + 1] << 5) ^ data[p + 2])
                          & (kHashSize - 1));
    }

    void Insert(size_t p)
    {
        uint32_t h = Hash(p);
        prev[p & (kWinSize - 1)] = head[h];
        head[h] = (uint32_t)p;
    }

    // Longest match for the string at pos, searching the hash chain.
    void FindMatch(size_t pos, int maxLen, int& bestLen, int& bestDist) const
    {
        bestLen = 0; bestDist = 0;
        if (maxLen < kMinMatch) return;

        uint32_t cand = head[Hash(pos)];
        int chain = kMaxChain;
        while (cand != kNil && chain-- > 0)
        {
            size_t c = cand;
            if (pos <= c || pos - c > (size_t)kWinSize) break;

            if (bestLen == 0 || data[c + (size_t)bestLen] == data[pos + (size_t)bestLen])
            {
                int l = 0;
                while (l < maxLen && data[c + (size_t)l] == data[pos + (size_t)l]) ++l;
                if (l > bestLen)
                {
                    bestLen = l;
                    bestDist = (int)(pos - c);
                    if (l >= kNiceMatch || l >= maxLen) break;
                }
            }
            cand = prev[c & (kWinSize - 1)];
        }
        if (bestLen < kMinMatch) { bestLen = 0; bestDist = 0; }
    }

    void PushLiteral(uint8_t b)
    {
        tokens.push_back(Token{ b, 0 });
        ++freqLit[b];
    }
    void PushMatch(int len, int dist)
    {
        tokens.push_back(Token{ (uint16_t)len, (uint16_t)dist });
        ++freqLit[257 + LengthCode(len)];
        ++freqDist[DistCode(dist)];
    }

    void Run()
    {
        if (size == 0)
        {
            // An empty member still needs one final block.
            bw.Bits(1, 1); bw.Bits(1, 2);      // BFINAL, BTYPE=01 fixed
            bw.Bits(0, 7);                     // fixed-Huffman end-of-block
            bw.Align();
            return;
        }

        size_t pos = 0;
        while (pos < size)
        {
            int bestLen = 0, bestDist = 0;
            const int maxLen = (int)std::min<size_t>(kMaxMatch, size - pos);
            bool inserted = false;   // pos must never enter the chain twice

            if (size - pos >= (size_t)kMinMatch)
            {
                FindMatch(pos, maxLen, bestLen, bestDist);

                // Lazy matching: if the next position starts a longer match,
                // emit this byte as a literal instead.
                if (bestLen >= kMinMatch && bestLen < kNiceMatch &&
                    pos + 1 + kMinMatch <= size)
                {
                    Insert(pos);
                    inserted = true;
                    int nLen = 0, nDist = 0;
                    const int nMax = (int)std::min<size_t>(kMaxMatch, size - (pos + 1));
                    FindMatch(pos + 1, nMax, nLen, nDist);
                    if (nLen > bestLen)
                    {
                        PushLiteral(data[pos]);
                        ++pos;
                        MaybeFlush(false);
                        continue;
                    }
                }
            }

            if (bestLen >= kMinMatch)
            {
                PushMatch(bestLen, bestDist);
                // Every position inside the match still needs indexing so
                // later matches can find it.
                const size_t end = pos + (size_t)bestLen;
                for (size_t p = pos; p < end; ++p)
                {
                    if (p == pos && inserted) continue;
                    if (p + kMinMatch <= size) Insert(p);
                }
                pos = end;
            }
            else
            {
                if (!inserted && pos + kMinMatch <= size) Insert(pos);
                PushLiteral(data[pos]);
                ++pos;
            }
            MaybeFlush(false);
        }
        MaybeFlush(true);
    }

    void MaybeFlush(bool last)
    {
        if (!last && tokens.size() < kBlockTokens) return;
        EmitBlock(last);
        tokens.clear();
        memset(freqLit, 0, sizeof freqLit);
        memset(freqDist, 0, sizeof freqDist);
    }

    void EmitBlock(bool last)
    {
        freqLit[256] = 1;                       // end-of-block must have a code
        EnsureTwoCodes(freqDist, kNumDist);

        uint8_t  litLens[kNumLitLen] = {};
        uint16_t litCodes[kNumLitLen] = {};
        uint8_t  distLens[kNumDist] = {};
        uint16_t distCodes[kNumDist] = {};

        BuildLengths(freqLit, kNumLitLen, 15, litLens);
        BuildCodes(litLens, kNumLitLen, 15, litCodes);
        BuildLengths(freqDist, kNumDist, 15, distLens);
        BuildCodes(distLens, kNumDist, 15, distCodes);

        int hlit = kNumLitLen;
        while (hlit > 257 && litLens[hlit - 1] == 0) --hlit;
        int hdist = kNumDist;
        while (hdist > 1 && distLens[hdist - 1] == 0) --hdist;

        // Run-length encode the concatenated code lengths.
        std::vector<uint8_t> lens;
        lens.reserve((size_t)hlit + hdist);
        lens.insert(lens.end(), litLens, litLens + hlit);
        lens.insert(lens.end(), distLens, distLens + hdist);

        struct ClSym { uint8_t sym; uint8_t extra; uint8_t extraBits; };
        std::vector<ClSym> cl;
        uint32_t freqCl[kNumCl] = {};

        for (size_t i = 0; i < lens.size();)
        {
            const uint8_t v = lens[i];
            size_t run = 1;
            while (i + run < lens.size() && lens[i + run] == v) ++run;

            if (v == 0)
            {
                while (run >= 11)
                {
                    size_t take = std::min<size_t>(run, 138);
                    cl.push_back({ 18, (uint8_t)(take - 11), 7 });
                    ++freqCl[18];
                    run -= take; i += take;
                }
                while (run >= 3)
                {
                    size_t take = std::min<size_t>(run, 10);
                    cl.push_back({ 17, (uint8_t)(take - 3), 3 });
                    ++freqCl[17];
                    run -= take; i += take;
                }
                for (size_t k = 0; k < run; ++k) { cl.push_back({ 0, 0, 0 }); ++freqCl[0]; }
                i += run;
            }
            else
            {
                cl.push_back({ v, 0, 0 });
                ++freqCl[v];
                --run; ++i;
                while (run >= 3)
                {
                    size_t take = std::min<size_t>(run, 6);
                    cl.push_back({ 16, (uint8_t)(take - 3), 2 });
                    ++freqCl[16];
                    run -= take; i += take;
                }
                for (size_t k = 0; k < run; ++k) { cl.push_back({ v, 0, 0 }); ++freqCl[v]; }
                i += run;
            }
        }

        EnsureTwoCodes(freqCl, kNumCl);
        uint8_t  clLens[kNumCl] = {};
        uint16_t clCodes[kNumCl] = {};
        BuildLengths(freqCl, kNumCl, 7, clLens);
        BuildCodes(clLens, kNumCl, 7, clCodes);

        int hclen = kNumCl;
        while (hclen > 4 && clLens[kClOrder[hclen - 1]] == 0) --hclen;

        bw.Bits(last ? 1 : 0, 1);
        bw.Bits(2, 2);                          // BTYPE = dynamic Huffman
        bw.Bits((uint32_t)(hlit - 257), 5);
        bw.Bits((uint32_t)(hdist - 1), 5);
        bw.Bits((uint32_t)(hclen - 4), 4);
        for (int i = 0; i < hclen; ++i) bw.Bits(clLens[kClOrder[i]], 3);

        for (const ClSym& c : cl)
        {
            bw.Bits(clCodes[c.sym], clLens[c.sym]);
            if (c.extraBits) bw.Bits(c.extra, c.extraBits);
        }

        for (const Token& t : tokens)
        {
            if (t.dist == 0)
            {
                bw.Bits(litCodes[t.litlen], litLens[t.litlen]);
            }
            else
            {
                const int lc = LengthCode(t.litlen);
                bw.Bits(litCodes[257 + lc], litLens[257 + lc]);
                if (kLenExtra[lc])
                    bw.Bits((uint32_t)(t.litlen - kLenBase[lc]), kLenExtra[lc]);

                const int dc = DistCode(t.dist);
                bw.Bits(distCodes[dc], distLens[dc]);
                if (kDistExtra[dc])
                    bw.Bits((uint32_t)(t.dist - kDistBase[dc]), kDistExtra[dc]);
            }
        }

        bw.Bits(litCodes[256], litLens[256]);   // end of block
        if (last) bw.Align();
    }
};

} // namespace

// ---------------------------------------------------------------------- public

void Deflate(const uint8_t* data, size_t n, std::vector<uint8_t>& out)
{
    Deflater d(data, n, out);
    d.Run();
}

uint32_t Crc32(const uint8_t* data, size_t n)
{
    static uint32_t table[256];
    static bool init = false;
    if (!init)
    {
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ------------------------------------------------------------------ GzipWriter

bool GzipWriter::Open(const std::wstring& path)
{
    Close();
    m_path = path;
    m_in.clear();
    m_in.reserve(1u << 20);
    m_compressed = 0;
    m_open = true;
    return true;
}

void GzipWriter::S(const char* s)
{
    if (!m_open || !s) return;
    const size_t n = strlen(s);
    m_in.insert(m_in.end(), (const uint8_t*)s, (const uint8_t*)s + n);
}

void GzipWriter::P(const char* fmt, ...)
{
    if (!m_open) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf_s(buf, sizeof buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n > 0) m_in.insert(m_in.end(), (const uint8_t*)buf, (const uint8_t*)buf + n);
}

bool GzipWriter::Close()
{
    if (!m_open) return true;
    m_open = false;

    std::vector<uint8_t> body;
    body.reserve(m_in.size() / 3 + 64);
    Deflate(m_in.data(), m_in.size(), body);

    FILE* f = nullptr;
    if (_wfopen_s(&f, m_path.c_str(), L"wb") != 0 || !f) return false;

    // RFC 1952 header: magic, deflate, no flags, mtime, no extra flags, unknown OS.
    const uint32_t mtime = (uint32_t)time(nullptr);
    const uint8_t header[10] = {
        0x1F, 0x8B, 0x08, 0x00,
        (uint8_t)(mtime & 0xFF), (uint8_t)((mtime >> 8) & 0xFF),
        (uint8_t)((mtime >> 16) & 0xFF), (uint8_t)((mtime >> 24) & 0xFF),
        0x00, 0xFF
    };
    fwrite(header, 1, sizeof header, f);
    fwrite(body.data(), 1, body.size(), f);

    const uint32_t crc = Crc32(m_in.data(), m_in.size());
    const uint32_t isize = (uint32_t)(m_in.size() & 0xFFFFFFFFu);
    const uint8_t footer[8] = {
        (uint8_t)(crc & 0xFF), (uint8_t)((crc >> 8) & 0xFF),
        (uint8_t)((crc >> 16) & 0xFF), (uint8_t)((crc >> 24) & 0xFF),
        (uint8_t)(isize & 0xFF), (uint8_t)((isize >> 8) & 0xFF),
        (uint8_t)((isize >> 16) & 0xFF), (uint8_t)((isize >> 24) & 0xFF)
    };
    fwrite(footer, 1, sizeof footer, f);

    const bool ok = ferror(f) == 0;
    fclose(f);

    m_compressed = sizeof header + body.size() + sizeof footer;
    m_in.clear();
    m_in.shrink_to_fit();
    return ok;
}
