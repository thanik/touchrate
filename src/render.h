// TouchRate - Direct3D 11 immediate renderer (flip-model, tearing-capable)
#pragma once
#include "common.h"
#include <d3d11.h>
#include <dxgi1_6.h>

enum FontId { F_TINY = 0, F_BODY = 1, F_HEAD = 2, F_HERO = 3, F_COUNT = 4 };

struct Glyph { float u0 = 0, v0 = 0, u1 = 0, v1 = 0, w = 0, h = 0, adv = 0; };
struct FontInfo { Glyph g[95]; float height = 0, ascent = 0, advance = 0; };

class Renderer
{
public:
    bool Init(HWND hwnd, UINT dpi);
    void Shutdown();
    void Resize(int w, int h);
    void SetDpi(UINT dpi);               // rebuilds the glyph atlas

    void BeginFrame(Color clear);
    void EndFrame();
    void Present(bool vsync);
    void WaitForPresentSlot();           // frame-latency gate, call before BeginFrame
    void UpdatePresentStats();

    int   Width()  const { return m_w; }
    int   Height() const { return m_h; }
    float Scale()  const { return m_scale; }
    bool  TearingSupported() const { return m_tearing; }
    bool  LastPresentTorn()  const { return m_lastTorn; }
    bool  Occluded() const { return m_occluded; }
    UINT  BufferCount() const { return m_bufferCount; }
    UINT  MaxFrameLatency() const { return m_maxLatency; }

    // Present statistics (meaningful with vsync on).
    bool     PresentStatsValid() const { return m_statsValid; }
    uint32_t PresentCount() const { return m_presentCount; }
    int64_t  PresentsDropped() const { return m_dropped; }

    IDXGIOutput* AcquireContainingOutput();   // caller releases
    ID3D11Device* Device() const { return m_dev; }
    const char*  AdapterName() const { return m_adapter.c_str(); }

    // ---- persistent ink layer
    // Accumulated touch samples are drawn once into an offscreen target and
    // then composited, so a long session costs nothing extra per frame.
    void BeginInk();
    void EndInk();
    void ClearInk();
    void CompositeInk();
    bool HasInkLayer() const { return m_inkRtv != nullptr; }

    // ---- clipping
    void SetClip(const Rect2& r);
    void ClearClip();

    // ---- primitives
    void FillRect(const Rect2& r, Color c);
    void FrameRect(const Rect2& r, float t, Color c);
    void Line(float x0, float y0, float x1, float y1, float t, Color c);
    void Disc(float cx, float cy, float rad, Color c);
    void Glow(float cx, float cy, float rad, Color c);
    void RingShape(float cx, float cy, float rad, Color c);
    void Cross(float cx, float cy, float size, float t, Color c);

    // ---- text
    float FontHeight(int f) const { return m_fonts[f].height; }
    float FontAdvance(int f) const { return m_fonts[f].advance; }
    float TextW(int f, const char* s) const;
    void  Text(int f, float x, float y, Color c, const char* s);
    void  Textf(int f, float x, float y, Color c, const char* fmt, ...);
    void  TextRight(int f, float xr, float y, Color c, const char* fmt, ...);
    void  TextCenter(int f, float cx, float y, Color c, const char* fmt, ...);

private:
    struct Vtx { float x, y, u, v, r, g, b, a; };

    bool CreatePipeline();
    bool CreateTargets();
    void ReleaseTargets();
    bool BuildAtlas();
    void Flush();
    void Quad(float x0, float y0, float x1, float y1,
              float u0, float v0, float u1, float v1, Color c);
    void TexQuad(const Rect2& dst, const Glyph& g, Color c);

    HWND  m_hwnd = nullptr;
    int   m_w = 1, m_h = 1;
    UINT  m_dpi = 96;
    float m_scale = 1.f;

    ID3D11Device*           m_dev = nullptr;
    ID3D11DeviceContext*    m_ctx = nullptr;
    IDXGIFactory2*          m_factory = nullptr;
    IDXGISwapChain1*        m_sc = nullptr;
    IDXGISwapChain2*        m_sc2 = nullptr;
    ID3D11RenderTargetView* m_rtv = nullptr;
    ID3D11Texture2D*        m_inkTex = nullptr;
    ID3D11RenderTargetView* m_inkRtv = nullptr;
    ID3D11ShaderResourceView* m_inkSrv = nullptr;
    ID3D11VertexShader*     m_vs = nullptr;
    ID3D11PixelShader*      m_ps = nullptr;
    ID3D11PixelShader*      m_psRgba = nullptr;
    ID3D11InputLayout*      m_il = nullptr;
    ID3D11Buffer*           m_vb = nullptr;
    ID3D11Buffer*           m_cb = nullptr;
    ID3D11BlendState*       m_blend = nullptr;
    ID3D11RasterizerState*  m_rs = nullptr;
    ID3D11SamplerState*     m_samp = nullptr;
    ID3D11Texture2D*        m_atlas = nullptr;
    ID3D11ShaderResourceView* m_atlasSrv = nullptr;
    HANDLE                  m_waitable = nullptr;

    bool  m_tearing = false;
    bool  m_lastTorn = false;
    bool  m_occluded = false;
    UINT  m_bufferCount = 3;
    UINT  m_maxLatency = 1;
    UINT  m_scFlags = 0;
    bool  m_statsValid = false;
    uint32_t m_presentCount = 0;
    int64_t  m_dropped = 0;
    uint32_t m_lastPresentCount = 0, m_lastRefreshCount = 0;
    bool     m_haveLastStats = false;
    std::string m_adapter;

    std::vector<Vtx> m_verts;
    FontInfo m_fonts[F_COUNT];
    Glyph    m_white, m_disc, m_glow, m_ring;
    int      m_atlasW = 2048, m_atlasH = 1024;
    Rect2    m_clip{};
    bool     m_clipped = false;
};
