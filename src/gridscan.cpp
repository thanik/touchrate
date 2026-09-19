#include "gridscan.h"

namespace {

// Cells across the long side. 40 gives roughly fingertip-sized cells on a
// 15-16" panel whatever its resolution.
const int kDensities[] = { 16, 20, 24, 32, 40, 48, 64, 80, 96 };
const int kNumDensities = (int)(sizeof kDensities / sizeof kDensities[0]);
const int kDefaultDensity = 4;

// Enough for well over an hour of ten-finger sweeping at 125 Hz.
const size_t kMaxPoints = 3000000;

// Boundary of cell i when an extent of n pixels is split into cells: ceil(i*n/cells).
// Using ceil here and floor in Bin() puts every integer pixel in exactly the
// cell whose rectangle contains it.
int Edge(int i, int n, int cells)
{
    return (int)(((int64_t)i * n + cells - 1) / cells);
}

} // namespace

GridScan::GridScan() : m_densityIdx(kDefaultDensity) {}

int GridScan::Density() const { return kDensities[m_densityIdx]; }

void GridScan::Clear()
{
    m_pts.clear();
    m_samples = 0;
    m_truncated = false;
    Rebuild();
}

void GridScan::SetArea(const RECT& a)
{
    if (a.right <= a.left || a.bottom <= a.top) return;
    if (a.left == m_area.left && a.top == m_area.top &&
        a.right == m_area.right && a.bottom == m_area.bottom && m_cols)
        return;
    m_area = a;
    Rebuild();
}

bool GridScan::Finer()
{
    if (m_densityIdx + 1 >= kNumDensities) return false;
    ++m_densityIdx;
    Rebuild();
    return true;
}

bool GridScan::Coarser()
{
    if (m_densityIdx <= 0) return false;
    --m_densityIdx;
    Rebuild();
    return true;
}

void GridScan::Rebuild()
{
    const int w = m_area.right - m_area.left, h = m_area.bottom - m_area.top;
    if (w <= 0 || h <= 0) { m_cols = m_rows = 0; m_hits.clear(); m_touched = 0; return; }

    // Split the long side into the chosen count and match the short side to
    // it, so cells come out close to square in either orientation.
    const int d = Density();
    if (w >= h) { m_cols = d; m_rows = std::max(1, (int)std::lround((double)d * h / w)); }
    else        { m_rows = d; m_cols = std::max(1, (int)std::lround((double)d * w / h)); }
    // A cell narrower than a pixel could never be touched and would read as a
    // dead zone; never make more cells than there are pixels.
    m_cols = std::min(m_cols, w);
    m_rows = std::min(m_rows, h);

    m_hits.assign((size_t)m_cols * m_rows, 0);
    m_touched = 0;
    m_maxHits = 0;
    for (const Pt& p : m_pts) Bin(p.x, p.y);
}

void GridScan::Add(int sx, int sy)
{
    ++m_samples;
    if (m_pts.size() < kMaxPoints) m_pts.push_back(Pt{ sx, sy });
    else m_truncated = true;
    Bin(sx, sy);
}

void GridScan::Bin(int sx, int sy)
{
    if (!m_cols) return;
    if (sx < m_area.left || sx >= m_area.right || sy < m_area.top || sy >= m_area.bottom) return;
    const int w = m_area.right - m_area.left, h = m_area.bottom - m_area.top;
    const int c = (int)((int64_t)(sx - m_area.left) * m_cols / w);
    const int r = (int)((int64_t)(sy - m_area.top) * m_rows / h);
    uint32_t& n = m_hits[(size_t)r * m_cols + c];
    if (n == 0) ++m_touched;
    if (n < UINT32_MAX) ++n;
    if (n > m_maxHits) m_maxHits = n;
}

RECT GridScan::CellRect(int c, int r) const
{
    const int w = m_area.right - m_area.left, h = m_area.bottom - m_area.top;
    RECT rc;
    rc.left   = m_area.left + Edge(c, w, m_cols);
    rc.right  = m_area.left + Edge(c + 1, w, m_cols);
    rc.top    = m_area.top  + Edge(r, h, m_rows);
    rc.bottom = m_area.top  + Edge(r + 1, h, m_rows);
    return rc;
}

bool GridScan::Frontier(int c, int r) const
{
    if (Hits(c, r)) return false;
    return (c > 0 && Hits(c - 1, r)) || (c + 1 < m_cols && Hits(c + 1, r)) ||
           (r > 0 && Hits(c, r - 1)) || (r + 1 < m_rows && Hits(c, r + 1));
}

void GridScan::EdgeSummary(Side& left, Side& top, Side& right, Side& bottom) const
{
    left = top = right = bottom = Side{};
    for (int r = 0; r < m_rows; ++r)
    {
        ++left.cells;  if (!Hits(0, r)) ++left.untouched;
        ++right.cells; if (!Hits(m_cols - 1, r)) ++right.untouched;
    }
    for (int c = 0; c < m_cols; ++c)
    {
        ++top.cells;    if (!Hits(c, 0)) ++top.untouched;
        ++bottom.cells; if (!Hits(c, m_rows - 1)) ++bottom.untouched;
    }
}
