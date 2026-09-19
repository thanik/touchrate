// TouchRate - grid scan (dead zone test)
//
// The screen is divided into cells and every delivered touch sample marks the
// cell it lands in. Once the whole screen has been swept, cells that never
// registered a touch are dead zones.
//
// Only reported sample positions mark a cell. Joining consecutive samples with
// a line would paint straight across a dead zone the finger swept over, which
// is exactly what this test has to expose.
#pragma once
#include "common.h"

class GridScan
{
public:
    GridScan();

    void Clear();                         // forget every sample
    void SetArea(const RECT& screenRect); // the monitor; rebins if it changed
    bool Finer();                         // smaller cells; false at the limit
    bool Coarser();                       // larger cells
    void Add(int sx, int sy);             // one sample, screen pixels

    const RECT& Area() const { return m_area; }
    int  Cols() const { return m_cols; }
    int  Rows() const { return m_rows; }
    int  Cells() const { return m_cols * m_rows; }
    int  Density() const;                 // cells across the long side

    uint32_t Hits(int c, int r) const { return m_hits[(size_t)r * m_cols + c]; }
    RECT CellRect(int c, int r) const;    // screen pixels, tiles the area exactly
    // Untouched, but next to a touched cell: a hole or dead strip rather than
    // somewhere the finger simply has not been yet.
    bool Frontier(int c, int r) const;

    int      Touched() const { return m_touched; }
    double   Coverage() const { return Cells() ? (double)m_touched / Cells() : 0.0; }
    uint64_t Samples() const { return m_samples; }
    uint32_t MaxHits() const { return m_maxHits; }
    bool     Truncated() const { return m_truncated; }

    struct Side { int untouched = 0, cells = 0; };
    // Untouched cells in the outermost row or column on each side.
    void EdgeSummary(Side& left, Side& top, Side& right, Side& bottom) const;

private:
    void Rebuild();
    void Bin(int sx, int sy);

    struct Pt { int32_t x, y; };

    RECT m_area{};
    int  m_densityIdx;
    int  m_cols = 0, m_rows = 0;
    std::vector<uint32_t> m_hits;
    int      m_touched = 0;
    uint32_t m_maxHits = 0;

    // Kept so a new cell size can be applied without losing the scan.
    std::vector<Pt> m_pts;
    uint64_t m_samples = 0;
    bool     m_truncated = false;   // too many samples to keep them all
};
